// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/skeleton_satin.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <utility>

#include "axis.hpp"
#include "axis_sampler.hpp"
#include "chord.hpp"
#include "geometry_detail.hpp"
#include "orientation.hpp"

namespace openstitch::auto_satin {

namespace {

using detail::Axis;
using detail::AxisParams;
using detail::ChordInterval;
using detail::OrientationKeys;
using detail::P2;
using detail::Poly;
using detail::SamplerContext;
using detail::SamplerParams;

P2 to_p2(Vec2um v) {
    return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
}

Vec2um to_um(P2 p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

// Éventail à la jonction de deux pièces. Un coude coupé en deux pièces perd la rotation
// qui se produit juste à la coupe : la dernière corde de la première pièce et la première
// de la suivante diffèrent de l'angle de virage et le secteur entre elles resterait sans
// fil. On y ajoute des cordes autour du point de coupe, l'angle suivant la rotation de la
// tangente, avec un pas angulaire tel que l'écart au bord extérieur vaut ρ.
std::vector<SkeletonSatinCrossing> junction_fan(const std::vector<Poly>& polys,
                                                const detail::AxisSample& prev,
                                                const detail::AxisSample& next,
                                                const SamplerParams& sp) {
    std::vector<SkeletonSatinCrossing> out;
    constexpr double kPi = 3.14159265358979323846;
    double turn = next.alpha - prev.alpha;
    while (turn > kPi) {
        turn -= 2.0 * kPi;
    }
    while (turn <= -kPi) {
        turn += 2.0 * kPi;
    }
    if (std::abs(turn) < 3.0 * kPi / 180.0) {
        return out;
    }
    const P2 mid{0.5 * (prev.p.x + next.p.x), 0.5 * (prev.p.y + next.p.y)};
    double theta = 0.0;
    int guard = 0;
    while (guard++ < 2000) {
        const double g = prev.g + theta;
        const P2 u{std::cos(g), std::sin(g)};
        const auto chord = chord_through(polys, mid, u, sp.tolerance_um);
        double reach = 500.0;
        if (chord) {
            reach = std::max({-chord->t_lo, chord->t_hi, 200.0});
        }
        theta += std::clamp(sp.spacing_um / reach, 0.005, 0.2) * (turn > 0.0 ? 1.0 : -1.0);
        if (std::abs(theta) >= std::abs(turn)) {
            break;
        }
        const double g2 = prev.g + theta;
        const P2 u2{std::cos(g2), std::sin(g2)};
        const auto c2 = chord_through(polys, mid, u2, sp.tolerance_um);
        if (!c2 || c2->length() < sp.min_chord_um) {
            continue;
        }
        // Seul le côté EXTÉRIEUR du virage est couvert par l'éventail : du côté intérieur,
        // les cordes se croiseraient (centre de rotation) et les cordes régulières des
        // pièces suffisent. La corde part donc d'un léger retrait côté intérieur.
        const double alphaMid = prev.alpha + theta;
        const P2 nLeft{-std::sin(alphaMid), std::cos(alphaMid)};
        const double outerSign = turn > 0.0 ? -1.0 : 1.0; // tourner à gauche : extérieur = droite
        const double along = (u2.x * nLeft.x + u2.y * nLeft.y) * outerSign >= 0.0 ? 1.0 : -1.0;
        const double tOuter = along > 0.0 ? c2->t_hi : -c2->t_lo;
        const double tInner = along > 0.0 ? -c2->t_lo : c2->t_hi;
        constexpr double kOverlap = 150.0;
        const double back = std::min(kOverlap, std::max(tInner, 0.0));
        if (tOuter + back < sp.min_chord_um) {
            continue;
        }
        const P2 dir{u2.x * along, u2.y * along};
        const P2 outerEnd{mid.x + dir.x * tOuter, mid.y + dir.y * tOuter};
        const P2 innerEnd{mid.x - dir.x * back, mid.y - dir.y * back};
        // Convention des traversées : a = côté droit, b = côté gauche de l'axe.
        const bool outerIsRight = outerSign < 0.0;
        out.push_back(outerIsRight ? SkeletonSatinCrossing{to_um(outerEnd), to_um(innerEnd)}
                                   : SkeletonSatinCrossing{to_um(innerEnd), to_um(outerEnd)});
    }
    return out;
}

// Intersection stricte de deux segments [p,p+r] et [q,q+w] : paramètres (t, u) si elle
// existe dans les deux intérieurs, sinon rien.
std::optional<std::pair<double, double>> segment_hit(P2 p, P2 r, P2 q, P2 w) {
    const double den = r.x * w.y - r.y * w.x;
    if (std::abs(den) < 1e-9) {
        return std::nullopt;
    }
    const double t = ((q.x - p.x) * w.y - (q.y - p.y) * w.x) / den;
    const double u = ((q.x - p.x) * r.y - (q.y - p.y) * r.x) / den;
    constexpr double kEps = 1e-6;
    if (t <= kEps || t >= 1.0 - kEps || u <= kEps || u >= 1.0 - kEps) {
        return std::nullopt;
    }
    return std::make_pair(t, u);
}

// Aucune corde ne doit en couper une autre : un croisement est du fil cousu en double
// et un nœud visible. Chaque corde est bornée, depuis son point d'axe, au premier
// croisement rencontré de chaque côté (les deux cordes concernées s'arrêtent au point
// de croisement). Les cordes devenues plus courtes que `min_len` sont retirées.
// Rend le nombre de cordes raccourcies.
int trim_crossings(std::vector<SkeletonSatinCrossing>& chords, const std::vector<P2>& axisPoints,
                   double min_len, double min_sep, bool closedRing) {
    const std::size_t n = chords.size();
    if (n < 3) {
        return 0;
    }
    // Chaque corde passe par son échantillon d'axe : on le retrouve comme l'intersection
    // de la corde avec la polyligne d'axe (milieu de corde à défaut, bouts prolongés).
    std::vector<P2> a(n), d(n), origin(n);
    std::vector<double> originT(n, 0.5);
    for (std::size_t i = 0; i < n; ++i) {
        a[i] = to_p2(chords[i].a);
        d[i] = to_p2(chords[i].b) - a[i];
        double best = 0.5;
        bool found = false;
        for (std::size_t k = 0; k + 1 < axisPoints.size(); ++k) {
            const P2 q = axisPoints[k];
            const P2 w = axisPoints[k + 1] - q;
            const double den = d[i].x * w.y - d[i].y * w.x;
            if (std::abs(den) < 1e-9) {
                continue;
            }
            const double t = ((q.x - a[i].x) * w.y - (q.y - a[i].y) * w.x) / den;
            const double u = ((q.x - a[i].x) * d[i].y - (q.y - a[i].y) * d[i].x) / den;
            if (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0) {
                best = t;
                found = true;
                break;
            }
        }
        originT[i] = found ? best : 0.5;
        origin[i] = a[i] + d[i] * originT[i];
    }
    std::vector<double> lo(n, 0.0), hi(n, 1.0); // bornes de t gardées
    std::vector<double> len(n);
    for (std::size_t i = 0; i < n; ++i) {
        len[i] = std::hypot(d[i].x, d[i].y);
    }
    // À chaque croisement, une seule des deux cordes est raccourcie : celle qui s'étend le
    // plus loin de son point d'axe avant de croiser (la corde qui déborde). L'autre reste
    // entière, donc elle continue de couvrir la zone ; on itère car raccourcir une corde
    // peut en libérer une autre.
    for (int round = 0; round < 40; ++round) {
        std::vector<double> nlo = lo, nhi = hi;
        bool any = false;
        for (std::size_t i = 0; i < n; ++i) {
            const P2 pi = a[i] + d[i] * lo[i];
            const P2 ri = d[i] * (hi[i] - lo[i]);
            const double minx = std::min(pi.x, pi.x + ri.x), maxx = std::max(pi.x, pi.x + ri.x);
            const double miny = std::min(pi.y, pi.y + ri.y), maxy = std::max(pi.y, pi.y + ri.y);
            for (std::size_t j = i + 1; j < n; ++j) {
                if (closedRing && (j == n - 1 && i == 0)) {
                    continue; // la clôture duplique la première corde
                }
                const P2 pj = a[j] + d[j] * lo[j];
                const P2 rj = d[j] * (hi[j] - lo[j]);
                if (std::min(pj.x, pj.x + rj.x) > maxx || std::max(pj.x, pj.x + rj.x) < minx ||
                    std::min(pj.y, pj.y + rj.y) > maxy || std::max(pj.y, pj.y + rj.y) < miny) {
                    continue;
                }
                const auto hit = segment_hit(pi, ri, pj, rj);
                if (!hit) {
                    continue;
                }
                const double ti = lo[i] + hit->first * (hi[i] - lo[i]);
                const double tj = lo[j] + hit->second * (hi[j] - lo[j]);
                const double di = std::abs(ti - originT[i]) * len[i];
                const double dj = std::abs(tj - originT[j]) * len[j];
                const bool victimI = di >= dj;
                const std::size_t v = victimI ? i : j;
                const double tv = victimI ? ti : tj;
                if (tv > originT[v]) {
                    nhi[v] = std::min(nhi[v], tv);
                } else {
                    nlo[v] = std::max(nlo[v], tv);
                }
                any = true;
            }
        }
        if (!any) {
            break;
        }
        lo = std::move(nlo);
        hi = std::move(nhi);
    }
    int trimmed = 0;
    std::vector<SkeletonSatinCrossing> kept;
    kept.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (lo[i] > 0.0 || hi[i] < 1.0) {
            ++trimmed;
        }
        const P2 na = a[i] + d[i] * lo[i];
        const P2 nb = a[i] + d[i] * hi[i];
        const double keptLen = std::hypot(nb.x - na.x, nb.y - na.y);
        if (keptLen < min_len) {
            continue;
        }
        kept.push_back(
            {lo[i] > 0.0 ? to_um(na) : chords[i].a, hi[i] < 1.0 ? to_um(nb) : chords[i].b});
    }
    chords = std::move(kept);
    // Deux cordes quasi confondues (recouvrement de cellules, pièces voisines) ne servent
    // qu'à coudre deux fois au même endroit : la plus courte est retirée. Un arrondi au
    // micromètre les ferait sinon se croiser sous un angle infime.
    const auto lineDist = [](P2 p, P2 q0, P2 q1) {
        const P2 w = q1 - q0;
        const double L = std::hypot(w.x, w.y);
        return L < 1e-9 ? std::hypot(p.x - q0.x, p.y - q0.y)
                        : std::abs((p.x - q0.x) * w.y - (p.y - q0.y) * w.x) / L;
    };
    std::vector<char> drop(chords.size(), 0);
    for (std::size_t i = 0; i < chords.size(); ++i) {
        if (drop[i]) {
            continue;
        }
        for (std::size_t j = i + 1; j < chords.size() && j <= i + 4; ++j) {
            if (drop[j]) {
                continue;
            }
            const P2 ai = to_p2(chords[i].a), bi = to_p2(chords[i].b);
            const P2 aj = to_p2(chords[j].a), bj = to_p2(chords[j].b);
            const double li = std::hypot(bi.x - ai.x, bi.y - ai.y);
            const double lj = std::hypot(bj.x - aj.x, bj.y - aj.y);
            const bool jShorter = lj <= li;
            const P2 s0 = jShorter ? aj : ai, s1 = jShorter ? bj : bi;
            const P2 l0 = jShorter ? ai : aj, l1 = jShorter ? bi : bj;
            if (lineDist(s0, l0, l1) < min_sep && lineDist(s1, l0, l1) < min_sep) {
                drop[jShorter ? j : i] = 1;
                if (!jShorter) {
                    break;
                }
            }
        }
    }
    std::vector<SkeletonSatinCrossing> unique;
    unique.reserve(chords.size());
    for (std::size_t i = 0; i < chords.size(); ++i) {
        if (!drop[i]) {
            unique.push_back(chords[i]);
        }
    }
    chords = std::move(unique);
    return trimmed;
}

// Chaîne d'arêtes du squelette entre deux nœuds qui ne sont pas des
// continuations : les nœuds de degré 2 ne sont pas des coupures.
struct Chain {
    std::vector<P2> points;
    std::vector<double> radii;
    bool free_start{false}; // le début est une extrémité libre (prolongée jusqu'au bord)
    bool free_end{false};
    bool closed{false}; // cycle sans extrémité (anneau) : axe périodique
};

std::vector<Chain> build_chains(const SkeletonGraph& g, SkeletonSatinDiagnostics& diag) {
    std::vector<Chain> chains;
    const std::size_t nodeCount = g.nodes.size();
    std::map<std::uint32_t, std::size_t> index;
    for (std::size_t i = 0; i < nodeCount; ++i) {
        index[g.nodes[i].id] = i;
    }
    // Arêtes incidentes à chaque nœud : (indice d'arête, vrai si le nœud est son `from`).
    std::vector<std::vector<std::pair<std::size_t, bool>>> incident(nodeCount);
    for (std::size_t e = 0; e < g.edges.size(); ++e) {
        incident[index.at(g.edges[e].from)].push_back({e, true});
        incident[index.at(g.edges[e].to)].push_back({e, false});
    }
    std::vector<char> used(g.edges.size(), 0);

    const auto append = [&](Chain& c, const SkeletonEdge& e, bool forward, bool skipFirst) {
        std::vector<P2> pts;
        std::vector<double> rad;
        pts.reserve(e.centerline.size());
        for (std::size_t i = 0; i < e.centerline.size(); ++i) {
            pts.push_back(to_p2(e.centerline[i]));
            rad.push_back(i < e.local_radii_um.size() ? e.local_radii_um[i] : 0.0);
        }
        if (!forward) {
            std::reverse(pts.begin(), pts.end());
            std::reverse(rad.begin(), rad.end());
        }
        const std::size_t from = skipFirst && !pts.empty() ? 1 : 0;
        c.points.insert(c.points.end(), pts.begin() + static_cast<std::ptrdiff_t>(from), pts.end());
        c.radii.insert(c.radii.end(), rad.begin() + static_cast<std::ptrdiff_t>(from), rad.end());
    };

    for (std::size_t n = 0; n < nodeCount; ++n) {
        const SkeletonNodeType type = g.nodes[n].type;
        if (type == SkeletonNodeType::Isolated) {
            diag.messages.push_back(
                "region compacte (noeud isole) : non eligible a l'auto-satin par squelette");
            continue;
        }
        if (type == SkeletonNodeType::Continuation) {
            continue;
        }
        for (const auto& [ei, atFrom] : incident[n]) {
            if (used[ei]) {
                continue;
            }
            used[ei] = 1;
            Chain c;
            c.free_start = type == SkeletonNodeType::Endpoint;
            append(c, g.edges[ei], atFrom, false);
            std::size_t cur = index.at(atFrom ? g.edges[ei].to : g.edges[ei].from);
            while (g.nodes[cur].type == SkeletonNodeType::Continuation) {
                bool found = false;
                for (const auto& [ej, atFromJ] : incident[cur]) {
                    if (used[ej]) {
                        continue;
                    }
                    used[ej] = 1;
                    append(c, g.edges[ej], atFromJ, true);
                    cur = index.at(atFromJ ? g.edges[ej].to : g.edges[ej].from);
                    found = true;
                    break;
                }
                if (!found) {
                    break;
                }
            }
            c.free_end = g.nodes[cur].type == SkeletonNodeType::Endpoint;
            chains.push_back(std::move(c));
        }
    }
    return chains;
}

// Composantes du squelette BRUT que le graphe n'a pas tracées : un cycle pur n'a
// aucun noeud, et `build_skeleton_graph` exclut le retour au pixel d'origine, donc
// une boucle J->J est perdue. On ne récupère que (a) un cycle fermé, ou (b) un arc
// dont les DEUX bouts touchent le graphe existant. Une composante à bout libre est
// une épine déjà élaguée par `prune_graph`, à ne pas faire revenir.
std::vector<Chain> recover_cycles(const AutoSatinDebug& dbg, const SkeletonGraph& graph,
                                  const std::vector<Chain>& chains) {
    std::vector<Chain> out;
    const RasterMask& sk = dbg.skeleton;
    const DistanceField& dist = dbg.distance;
    const int w = sk.width;
    const int h = sk.height;
    if (w <= 0 || h <= 0) {
        return out;
    }
    const auto idx = [&](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
               static_cast<std::size_t>(x);
    };
    const auto col_of = [&](P2 q) {
        return static_cast<int>(
            std::lround((q.x - sk.transform.min_x_um) / sk.transform.pixel_size_um));
    };
    const auto row_of = [&](P2 q) {
        return static_cast<int>(
            std::lround((sk.transform.max_y_um - q.y) / sk.transform.pixel_size_um));
    };
    std::vector<char> covered(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    const auto mark = [&](P2 q) {
        const int cx = col_of(q);
        const int cy = row_of(q);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = cx + dx;
                const int y = cy + dy;
                if (x >= 0 && y >= 0 && x < w && y < h) {
                    covered[idx(x, y)] = 1;
                }
            }
        }
    };
    for (const Chain& c : chains) {
        for (const P2& q : c.points) {
            mark(q);
        }
    }
    for (const SkeletonNode& n : graph.nodes) {
        mark(to_p2(n.position));
    }
    const auto touches_covered = [&](int x, int y) {
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < w && ny < h && covered[idx(nx, ny)]) {
                    return true;
                }
            }
        }
        return false;
    };
    // Voisinage 8, 4-voisins d'abord (ordre fixe : déterminisme).
    constexpr int kDx[8] = {1, 0, -1, 0, 1, -1, -1, 1};
    constexpr int kDy[8] = {0, 1, 0, -1, 1, 1, -1, -1};

    std::vector<char> seen(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!sk.at(x, y) || covered[idx(x, y)] || seen[idx(x, y)]) {
                continue;
            }
            // Composante connexe des pixels de squelette non couverts.
            std::vector<std::pair<int, int>> comp{{x, y}};
            seen[idx(x, y)] = 1;
            for (std::size_t qi = 0; qi < comp.size(); ++qi) {
                for (int k = 0; k < 8; ++k) {
                    const int nx = comp[qi].first + kDx[k];
                    const int ny = comp[qi].second + kDy[k];
                    if (sk.at(nx, ny) && !covered[idx(nx, ny)] && !seen[idx(nx, ny)]) {
                        seen[idx(nx, ny)] = 1;
                        comp.push_back({nx, ny});
                    }
                }
            }
            if (comp.size() < 12) {
                continue; // bruit
            }
            const auto compIndex = [&](int px, int py) -> int {
                for (std::size_t i = 0; i < comp.size(); ++i) {
                    if (comp[i].first == px && comp[i].second == py) {
                        return static_cast<int>(i);
                    }
                }
                return -1;
            };
            // Extrémités : pixels n'ayant qu'un voisin dans la composante.
            std::vector<std::pair<int, int>> ends;
            for (const auto& q : comp) {
                int nb = 0;
                for (int k = 0; k < 8; ++k) {
                    nb += compIndex(q.first + kDx[k], q.second + kDy[k]) >= 0 ? 1 : 0;
                }
                if (nb <= 1) {
                    ends.push_back(q);
                }
            }
            const bool closedLoop = ends.empty();
            if (!closedLoop) {
                const bool ok = ends.size() == 2 &&
                                touches_covered(ends[0].first, ends[0].second) &&
                                touches_covered(ends[1].first, ends[1].second);
                if (!ok) {
                    continue; // épine élaguée, ou forme non reconnue
                }
            }
            // Parcours ordonné depuis une extrémité (arc) ou le premier pixel (cycle).
            std::vector<char> walked(comp.size(), 0);
            std::pair<int, int> cur = closedLoop ? comp.front() : ends.front();
            Chain chain;
            chain.closed = closedLoop;
            while (true) {
                const int ci = compIndex(cur.first, cur.second);
                walked[static_cast<std::size_t>(ci)] = 1;
                chain.points.push_back(to_p2(sk.transform.to_um(static_cast<double>(cur.first),
                                                                static_cast<double>(cur.second))));
                chain.radii.push_back(static_cast<double>(dist.at(cur.first, cur.second)));
                bool advanced = false;
                for (int k = 0; k < 8; ++k) {
                    const int nx = cur.first + kDx[k];
                    const int ny = cur.second + kDy[k];
                    const int ni = compIndex(nx, ny);
                    if (ni >= 0 && !walked[static_cast<std::size_t>(ni)]) {
                        cur = {nx, ny};
                        advanced = true;
                        break;
                    }
                }
                if (!advanced) {
                    break;
                }
            }
            if (chain.points.size() >= 12) {
                out.push_back(std::move(chain));
            }
        }
    }
    return out;
}

double wrap_pi(double a) {
    return std::remainder(a, 2.0 * std::numbers::pi);
}

// Abscisses de coupe d'un axe aux coudes : changement de cap mesuré sur une
// fenêtre ±W, au-dessus du seuil, séparés d'au moins W. Quand le coin entier tient
// dans la fenêtre, la mesure forme un PLATEAU (égal à l'angle du coin) : on coupe
// au CENTRE du plateau, pas à son premier point -- couper W trop tôt fait démarrer
// le morceau suivant dans la partie droite du bras, avec une tangente fausse.
std::vector<double> bend_cuts(const Axis& axis, double window, double threshold_rad) {
    std::vector<double> cuts;
    const double length = axis.length();
    if (length < 3.0 * window) {
        return cuts;
    }
    constexpr double kStep = 100.0;
    constexpr double kPlateauTol = 0.02;        // rad
    std::vector<std::pair<double, double>> run; // (s, changement de cap)
    const auto flush = [&]() {
        if (run.empty()) {
            return;
        }
        double best = 0.0;
        for (const auto& [s, d] : run) {
            best = std::max(best, d);
        }
        double first = run.front().first;
        double last = run.back().first;
        bool seen = false;
        for (const auto& [s, d] : run) {
            if (d >= best - kPlateauTol) {
                if (!seen) {
                    first = s;
                    seen = true;
                }
                last = s;
            }
        }
        const double cut = 0.5 * (first + last);
        if (cuts.empty() || cut - cuts.back() >= window) {
            cuts.push_back(cut);
        }
        run.clear();
    };
    for (double s = window; s <= length - window + 1e-9; s += kStep) {
        const double d = std::abs(wrap_pi(axis.alpha(s + window) - axis.alpha(s - window)));
        if (d >= threshold_rad) {
            run.push_back({s, d});
        } else {
            flush();
        }
    }
    flush();
    return cuts;
}

// Morceau de colonne entre deux coudes, support d'une cellule.
struct Piece {
    Axis axis;
    std::size_t chain{0};
    double chain_offset{0.0};
    bool extend_start{false};
    bool extend_end{false};
    std::vector<P2> site; // polyligne de la cellule
    P2 bbox_min{};
    P2 bbox_max{};
};

double distance_to_polyline(const std::vector<P2>& pts, P2 q) {
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const P2 ab = pts[i + 1] - pts[i];
        const double len2 = detail::dot(ab, ab);
        const double t =
            len2 > 1e-12 ? std::clamp(detail::dot(q - pts[i], ab) / len2, 0.0, 1.0) : 0.0;
        best = std::min(best, detail::norm(q - (pts[i] + ab * t)));
    }
    return best;
}

double distance_to_bbox(const Piece& p, P2 q) {
    const double dx = std::max({p.bbox_min.x - q.x, 0.0, q.x - p.bbox_max.x});
    const double dy = std::max({p.bbox_min.y - q.y, 0.0, q.y - p.bbox_max.y});
    return std::hypot(dx, dy);
}

// Vrai si une autre cellule est strictement plus proche de q que `limit`.
bool other_cell_closer(const std::vector<Piece>& pieces, std::size_t own, P2 q, double limit) {
    for (std::size_t k = 0; k < pieces.size(); ++k) {
        if (k == own || distance_to_bbox(pieces[k], q) >= limit) {
            continue;
        }
        if (distance_to_polyline(pieces[k].site, q) < limit) {
            return true;
        }
    }
    return false;
}

// Écrête la corde `raw` (le long de p + t·u) à la cellule du morceau `own` :
// ensemble des points dont le morceau le plus proche est `own`, élargi de `bias`
// (recouvrement entre cellules voisines).
std::optional<ChordInterval> clip_to_cell(const std::vector<Piece>& pieces, std::size_t own, P2 p,
                                          P2 u, const ChordInterval& raw, double bias) {
    constexpr double kStep = 40.0;
    constexpr int kRefine = 10;
    const auto inside = [&](double t) {
        const P2 q = p + u * t;
        const double d = distance_to_polyline(pieces[own].site, q);
        return !other_cell_closer(pieces, own, q, d - bias);
    };
    if (!inside(0.0)) {
        return std::nullopt;
    }
    const auto limit = [&](double dir, double tmax) {
        double good = 0.0;
        double t = 0.0;
        while (t < tmax) {
            const double nt = std::min(t + kStep, tmax);
            if (!inside(dir * nt)) {
                double lo = good;
                double hi = nt;
                for (int i = 0; i < kRefine; ++i) {
                    const double mid = 0.5 * (lo + hi);
                    (inside(dir * mid) ? lo : hi) = mid;
                }
                return lo;
            }
            good = nt;
            t = nt;
        }
        return tmax;
    };
    const double fwd = limit(1.0, std::max(0.0, raw.t_hi));
    const double bwd = limit(-1.0, std::max(0.0, -raw.t_lo));
    return ChordInterval{-bwd, fwd};
}

// Plus proche abscisse de l'axe pour un point (projection sur la polyligne).
std::pair<double, double> project_on_axis(const Axis& axis, P2 q) {
    const auto& pts = axis.points();
    double bestDist = std::numeric_limits<double>::max();
    double bestS = 0.0;
    double cum = 0.0;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const P2 ab = pts[i + 1] - pts[i];
        const double len = detail::norm(ab);
        const double len2 = len * len;
        const double t =
            len2 > 1e-12 ? std::clamp(detail::dot(q - pts[i], ab) / len2, 0.0, 1.0) : 0.0;
        const double d = detail::norm(q - (pts[i] + ab * t));
        if (d < bestDist) {
            bestDist = d;
            bestS = cum + t * len;
        }
        cum += len;
    }
    return {bestS, bestDist};
}

// Couverture estimée : rasterise les triangles balayés par deux traversées
// consécutives d'une même colonne (rendu des fils) sur le masque de la région.
void measure_coverage(const geometry::PathSet& region, SkeletonSatinResult& result) {
    SkeletonRasterParameters raster;
    raster.pixel_size = Micrometers{100};
    const auto mask = rasterize(region, raster);
    if (!mask || mask->width <= 0 || mask->height <= 0) {
        return;
    }
    const RasterMask& m = *mask;
    std::vector<std::uint8_t> count(m.pixels.size(), 0);
    const double pix = m.transform.pixel_size_um;
    const auto fill = [&](P2 a, P2 b, P2 c) {
        const auto toCol = [&](double x) { return (x - m.transform.min_x_um) / pix; };
        const auto toRow = [&](double y) { return (m.transform.max_y_um - y) / pix; };
        const double ax = toCol(a.x), ay = toRow(a.y);
        const double bx = toCol(b.x), by = toRow(b.y);
        const double cx = toCol(c.x), cy = toRow(c.y);
        const double det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::abs(det) < 1e-9) {
            return;
        }
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}))));
        const int x1 = std::min(m.width - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy}))));
        const int y1 = std::min(m.height - 1, static_cast<int>(std::ceil(std::max({ay, by, cy}))));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const double l1 = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / det;
                const double l2 = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / det;
                const double l3 = 1.0 - l1 - l2;
                if (l1 >= 0.0 && l2 >= 0.0 && l3 >= 0.0) {
                    auto& v =
                        count[static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) +
                              static_cast<std::size_t>(x)];
                    if (v < 255) {
                        ++v;
                    }
                }
            }
        }
    };
    for (const auto& column : result.columns) {
        for (std::size_t i = 0; i + 1 < column.crossings.size(); ++i) {
            const P2 a0 = to_p2(column.crossings[i].a);
            const P2 b0 = to_p2(column.crossings[i].b);
            const P2 a1 = to_p2(column.crossings[i + 1].a);
            const P2 b1 = to_p2(column.crossings[i + 1].b);
            fill(a0, b0, b1);
            fill(a0, b1, a1);
        }
    }
    // Mesure sur le masque ÉRODÉ d'un pixel : les pixels du contour sont à moitié
    // couverts par construction (les cordes s'arrêtent sur le bord), ce qui
    // sous-estimerait la couverture d'autant plus que le périmètre est grand.
    std::size_t inside = 0;
    std::size_t covered = 0;
    std::size_t total = 0;
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x < m.width; ++x) {
            if (!m.at(x, y) || !m.at(x - 1, y) || !m.at(x + 1, y) || !m.at(x, y - 1) ||
                !m.at(x, y + 1)) {
                continue;
            }
            const auto v = count[static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) +
                                 static_cast<std::size_t>(x)];
            ++inside;
            covered += v > 0 ? 1 : 0;
            total += v;
        }
    }
    auto& d = result.diagnostics;
    if (inside == 0) {
        return;
    }
    d.coverage_measured = true;
    d.coverage_ratio = static_cast<double>(covered) / static_cast<double>(inside);
    d.overlap_ratio = static_cast<double>(total) / static_cast<double>(inside);
    d.uncovered_area_mm2 = static_cast<double>(inside - covered) * pix * pix / 1e6;
}

} // namespace

Result<SkeletonSatinResult> generate_skeleton_satin(const geometry::PathSet& region,
                                                    const SkeletonSatinParameters& params) {
    SkeletonSatinResult result;
    auto& diag = result.diagnostics;

    const auto analysis = analyze_region(region, params.analysis);
    if (!analysis) {
        return std::unexpected(analysis.error());
    }
    const SkeletonGraph& graph = analysis->debug.graph;
    const std::vector<Poly> polys = detail::region_polys(region);
    if (polys.empty()) {
        return result;
    }

    std::vector<Chain> chains = build_chains(graph, diag);
    for (auto& c : recover_cycles(analysis->debug, graph, chains)) {
        chains.push_back(std::move(c));
    }
    if (chains.empty() && diag.messages.empty()) {
        diag.messages.push_back("region compacte ou sans axe exploitable (disque, forme peu "
                                "allongee) : non eligible a l'auto-satin par squelette");
    }

    const double window_floor = 600.0;
    const double bend_rad = params.bend_threshold_deg * std::numbers::pi / 180.0;

    // Axes de chaîne, coupe aux coudes, morceaux.
    std::vector<Axis> chainAxes;
    chainAxes.reserve(chains.size());
    std::vector<Piece> pieces;
    std::vector<std::pair<std::size_t, std::size_t>> chainPieces; // [début, fin) dans pieces
    for (std::size_t ci = 0; ci < chains.size(); ++ci) {
        const Chain& c = chains[ci];
        Axis axis = c.closed ? Axis::build_closed(c.points, AxisParams{})
                             : Axis::build(c.points, AxisParams{});
        const std::size_t first = pieces.size();
        if (!axis.empty() && c.closed) {
            Piece piece;
            piece.axis = axis;
            piece.chain = ci;
            piece.site = axis.points();
            piece.bbox_min = piece.bbox_max = piece.site.front();
            for (const P2& q : piece.site) {
                piece.bbox_min = {std::min(piece.bbox_min.x, q.x), std::min(piece.bbox_min.y, q.y)};
                piece.bbox_max = {std::max(piece.bbox_max.x, q.x), std::max(piece.bbox_max.y, q.y)};
            }
            pieces.push_back(std::move(piece));
        } else if (!axis.empty()) {
            double meanRadius = 0.0;
            for (double r : c.radii) {
                meanRadius += r;
            }
            meanRadius = c.radii.empty() ? 500.0 : meanRadius / static_cast<double>(c.radii.size());
            std::vector<double> bounds{0.0};
            for (double cut : bend_cuts(axis, std::max(window_floor, meanRadius), bend_rad)) {
                bounds.push_back(cut);
            }
            bounds.push_back(axis.length());
            for (std::size_t b = 0; b + 1 < bounds.size(); ++b) {
                std::vector<P2> raw;
                for (double s = bounds[b]; s < bounds[b + 1]; s += 50.0) {
                    raw.push_back(axis.position(s));
                }
                raw.push_back(axis.position(bounds[b + 1]));
                AxisParams ap;
                ap.smooth_passes = 0;
                Piece piece;
                piece.axis = Axis::build(raw, ap);
                if (piece.axis.empty()) {
                    continue;
                }
                piece.chain = ci;
                piece.chain_offset = bounds[b];
                piece.extend_start = b == 0 && c.free_start;
                piece.extend_end = b + 2 == bounds.size() && c.free_end;
                piece.site = piece.axis.points();
                piece.bbox_min = piece.bbox_max = piece.site.front();
                for (const P2& q : piece.site) {
                    piece.bbox_min = {std::min(piece.bbox_min.x, q.x),
                                      std::min(piece.bbox_min.y, q.y)};
                    piece.bbox_max = {std::max(piece.bbox_max.x, q.x),
                                      std::max(piece.bbox_max.y, q.y)};
                }
                pieces.push_back(std::move(piece));
            }
        } else {
            diag.messages.push_back("branche degeneree ignoree");
        }
        chainAxes.push_back(std::move(axis));
        chainPieces.push_back({first, pieces.size()});
    }
    diag.pieces = static_cast<int>(pieces.size());

    // Guides : projetés sur chaque chaîne assez proche.
    std::vector<char> guideUsed(params.guides.size(), 0);
    std::vector<OrientationKeys> chainKeys(chains.size());
    for (std::size_t ci = 0; ci < chains.size(); ++ci) {
        const Axis& axis = chainAxes[ci];
        if (axis.empty()) {
            continue;
        }
        double meanRadius = 500.0;
        if (!chains[ci].radii.empty()) {
            meanRadius = 0.0;
            for (double r : chains[ci].radii) {
                meanRadius += r;
            }
            meanRadius /= static_cast<double>(chains[ci].radii.size());
        }
        const double reach = std::max(2.0 * meanRadius, 1500.0);
        std::vector<std::pair<double, std::size_t>> hits;
        for (std::size_t gi = 0; gi < params.guides.size(); ++gi) {
            const auto [s, d] = project_on_axis(axis, to_p2(params.guides[gi].anchor));
            if (d <= reach) {
                hits.push_back({s, gi});
                guideUsed[gi] = 1;
            }
        }
        std::stable_sort(hits.begin(), hits.end(),
                         [](const auto& x, const auto& y) { return x.first < y.first; });
        for (const auto& [s, gi] : hits) {
            const auto& gd = params.guides[gi];
            chainKeys[ci].keys.push_back({s, gd.absolute, gd.angle_rad});
            chainKeys[ci].alpha.push_back(axis.alpha(s));
        }
    }
    for (char used : guideUsed) {
        if (!used) {
            ++diag.orphan_guides;
        }
    }

    SamplerParams sp;
    sp.spacing_um = static_cast<double>(params.spacing.value);
    sp.min_chord_um = static_cast<double>(params.min_thread_length.value);
    const double bias = static_cast<double>(params.cell_overlap.value);

    result.columns.reserve(chains.size());
    for (std::size_t ci = 0; ci < chains.size(); ++ci) {
        SkeletonSatinColumn column;
        std::optional<detail::AxisSample> prevLast;
        for (std::size_t pi = chainPieces[ci].first; pi < chainPieces[ci].second; ++pi) {
            const Piece& piece = pieces[pi];
            OrientationKeys keys;
            keys.alpha = chainKeys[ci].alpha;
            for (const auto& k : chainKeys[ci].keys) {
                keys.keys.push_back({k.s - piece.chain_offset, k.absolute, k.value});
            }
            SamplerContext ctx;
            ctx.extend_start = piece.extend_start;
            ctx.extend_end = piece.extend_end;
            if (pieces.size() > 1) {
                ctx.clip = [&pieces, pi, bias](double, P2 p, P2 u, const ChordInterval& raw) {
                    return clip_to_cell(pieces, pi, p, u, raw, bias);
                };
            }
            const auto sampled = detail::sample_axis(piece.axis, polys, keys, sp, ctx);
            diag.outside_samples += sampled.diagnostics.outside_samples;
            diag.too_short += sampled.diagnostics.too_short;
            diag.clamped_angle += sampled.diagnostics.clamped_angle;
            diag.radius_guard_hits += sampled.diagnostics.radius_guard_hits;
            if (prevLast && !sampled.samples.empty()) {
                for (const auto& fc : junction_fan(polys, *prevLast, sampled.samples.front(), sp)) {
                    column.crossings.push_back(fc);
                    ++diag.fan_chords;
                }
            }
            for (const auto& smp : sampled.samples) {
                column.crossings.push_back({to_um(smp.a), to_um(smp.b)});
            }
            if (!sampled.samples.empty()) {
                prevLast = sampled.samples.back();
            }
        }
        if (!column.crossings.empty()) {
            std::vector<Vec2um> axisPts;
            axisPts.reserve(chainAxes[ci].points().size());
            for (const P2& q : chainAxes[ci].points()) {
                axisPts.push_back(to_um(q));
            }
            result.axes.push_back(std::move(axisPts));
            diag.trimmed_crossings +=
                trim_crossings(column.crossings, chainAxes[ci].points(),
                               static_cast<double>(params.min_thread_length.value),
                               0.25 * static_cast<double>(params.spacing.value), chains[ci].closed);
            if (chains[ci].closed && !column.crossings.empty()) {
                column.crossings.push_back(column.crossings.front()); // clôture à la couture
            }
            result.columns.push_back(std::move(column));
        }
    }
    if (params.measure_coverage) {
        measure_coverage(region, result);
    }
    return result;
}

std::string skeleton_satin_to_svg(const geometry::PathSet& region,
                                  const SkeletonSatinResult& result) {
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    const auto grow = [&](Vec2um v) {
        const double x = v.x.value / 1000.0;
        const double y = -v.y.value / 1000.0;
        minx = std::min(minx, x);
        maxx = std::max(maxx, x);
        miny = std::min(miny, y);
        maxy = std::max(maxy, y);
    };
    for (const auto& n : region.outer.nodes) {
        grow(n.pos);
    }
    if (minx > maxx) {
        return "<svg xmlns=\"http://www.w3.org/2000/svg\"/>\n";
    }
    const auto num = [](double v) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%.3f", v);
        return std::string(buf);
    };
    const auto pt = [&](Vec2um v) {
        return num(v.x.value / 1000.0) + "," + num(-v.y.value / 1000.0);
    };
    const double margin = 2.0;
    std::string svg;
    svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" + num(minx - margin) + " " +
           num(miny - margin) + " " + num(maxx - minx + 2 * margin) + " " +
           num(maxy - miny + 2 * margin) + "\">\n";
    const auto ring = [&](const geometry::Path& path) {
        std::string d;
        for (std::size_t i = 0; i < path.nodes.size(); ++i) {
            d += (i == 0 ? "M" : "L") + pt(path.nodes[i].pos);
        }
        return d + "Z";
    };
    svg += "<path d=\"" + ring(region.outer);
    for (const auto& hole : region.holes) {
        svg += ring(hole);
    }
    svg += "\" fill=\"#f4f6fa\" stroke=\"#8a94a6\" stroke-width=\"0.15\" fill-rule=\"evenodd\"/>\n";
    // Traversées.
    svg += "<path d=\"";
    for (const auto& column : result.columns) {
        for (const auto& c : column.crossings) {
            svg += "M" + pt(c.a) + "L" + pt(c.b);
        }
    }
    svg += "\" fill=\"none\" stroke=\"#2f6fdc\" stroke-width=\"0.06\" stroke-opacity=\"0.7\"/>\n";
    // Zigzag cousu de chaque colonne : A0 B0 A1 B1 ...
    for (const auto& column : result.columns) {
        svg += "<polyline fill=\"none\" stroke=\"#d9472b\" stroke-width=\"0.04\" "
               "stroke-opacity=\"0.5\" points=\"";
        for (const auto& c : column.crossings) {
            svg += pt(c.a) + " " + pt(c.b) + " ";
        }
        svg += "\"/>\n";
    }
    // Axes de référence.
    for (const auto& axis : result.axes) {
        svg += "<polyline fill=\"none\" stroke=\"#1c8a4a\" stroke-width=\"0.12\" "
               "stroke-dasharray=\"0.6 0.4\" points=\"";
        for (const auto& v : axis) {
            svg += pt(v) + " ";
        }
        svg += "\"/>\n";
    }
    svg += "</svg>\n";
    return svg;
}

} // namespace openstitch::auto_satin
