// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/tatami.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

#include "openstitch/geometry/offset.hpp"
#include "openstitch/stitch_generation/running_stitch.hpp"

namespace openstitch::stitch_generation {

namespace {

struct PointD {
    double x{0.0};
    double y{0.0};
};

// Rotation d'un point du repère modèle vers le repère « rangées horizontales »
// (rotation de -angle) et retour (+angle).
PointD rotate(PointD p, double cosA, double sinA) {
    return {p.x * cosA - p.y * sinA, p.x * sinA + p.y * cosA};
}

// Intersections d'une horizontale y=scanY avec un polygone fermé (liste
// d'arêtes), ajoutées à `xs`. Convention demi-ouverte sur y pour ne pas
// compter deux fois un sommet.
void scan_polygon(const std::vector<PointD>& poly, double scanY, std::vector<double>& xs) {
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const PointD a = poly[i];
        const PointD b = poly[(i + 1) % n];
        const double y0 = a.y;
        const double y1 = b.y;
        if ((y0 <= scanY && y1 > scanY) || (y1 <= scanY && y0 > scanY)) {
            const double t = (scanY - y0) / (y1 - y0);
            xs.push_back(a.x + t * (b.x - a.x));
        }
    }
}

Vec2um to_um(PointD p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

// Point dans polygone (lancer de rayon horizontal, règle pair-impair).
bool point_in_poly(const std::vector<PointD>& poly, PointD p) {
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const PointD a = poly[i];
        const PointD b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) {
            inside = !inside;
        }
    }
    return inside;
}

// Distance au carré de p au segment [c,d].
double point_seg_dist2(PointD p, PointD c, PointD d) {
    const double dx = d.x - c.x;
    const double dy = d.y - c.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-12) {
        const double ex = p.x - c.x;
        const double ey = p.y - c.y;
        return ex * ex + ey * ey;
    }
    double t = ((p.x - c.x) * dx + (p.y - c.y) * dy) / len2;
    t = std::clamp(t, 0.0, 1.0);
    const double fx = p.x - (c.x + t * dx);
    const double fy = p.y - (c.y + t * dy);
    return fx * fx + fy * fy;
}

// p est-il (quasi) SUR une arête d'un des polygones (extérieur ou trou) ? Le
// test pair-impair est ambigu pile sur une frontière — pour un trou, il classe
// même le point comme « dans le trou » (cf. arête opposée qui bascule seule la
// parité), ce qui rejetterait à tort un suivi de bord légitime. On traite donc
// tout point sur une frontière comme faisant partie de la région, AVANT le
// test pair-impair.
bool point_on_boundary(const std::vector<std::vector<PointD>>& polys, PointD p) {
    constexpr double kEps2 = 1e-4;  // 0,01 µm : marge numérique, pas une tolérance géométrique
    for (const auto& poly : polys) {
        const std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            if (point_seg_dist2(p, poly[i], poly[(i + 1) % n]) < kEps2) {
                return true;
            }
        }
    }
    return false;
}

// p est-il dans la région = dans l'extérieur ET hors de tous les trous ?
bool in_region(const std::vector<std::vector<PointD>>& polys, PointD p) {
    if (polys.empty()) {
        return false;
    }
    if (point_on_boundary(polys, p)) {
        return true;
    }
    if (!point_in_poly(polys[0], p)) {
        return false;
    }
    for (std::size_t i = 1; i < polys.size(); ++i) {
        if (point_in_poly(polys[i], p)) {
            return false;
        }
    }
    return true;
}

// La liaison [a,b] (dans le repère des rangées) est-elle un trajet cousu
// INVALIDE ? Robuste à l'orientation du segment et aux contacts par
// sommet/extrémité : on découpe [a,b] à CHAQUE intersection paramétrique avec
// une arête d'un polygone (extérieur ou trou), y COMPRIS les contacts
// dégénérés (le segment passe exactement par un sommet), puis on vérifie que
// le MILIEU de chaque sous-segment ainsi obtenu reste dans la région. Une
// arête colinéaire au segment (suivi de bord/frontière) ne produit aucune
// découpe : longer un bord reste autorisé. Ce test ne dépend d'aucun seuil sur
// l'écart en x — un connecteur parfaitement vertical qui traverse un trou de
// part en part (même en touchant ses sommets, sans jamais le « croiser »
// franchement) est donc détecté.
bool connector_invalid(const std::vector<std::vector<PointD>>& polys, PointD a, PointD b) {
    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double abLen = std::sqrt(abx * abx + aby * aby);
    if (abLen < 1e-6) {
        return !in_region(polys, a);
    }
    std::vector<double> ts{0.0, 1.0};
    for (const auto& poly : polys) {
        const std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            const PointD c = poly[i];
            const PointD d = poly[(i + 1) % n];
            const double dcx = d.x - c.x;
            const double dcy = d.y - c.y;
            const double cdLen = std::sqrt(dcx * dcx + dcy * dcy);
            const double denom = abx * dcy - aby * dcx;
            if (std::abs(denom) < 1e-9 * abLen * cdLen) {
                continue;  // parallèle/colinéaire : suivi de bord, pas de découpe
            }
            const double t = ((c.x - a.x) * dcy - (c.y - a.y) * dcx) / denom;
            const double s = ((c.x - a.x) * aby - (c.y - a.y) * abx) / denom;
            if (t > 1e-9 && t < 1.0 - 1e-9 && s >= -1e-9 && s <= 1.0 + 1e-9) {
                ts.push_back(t);
            }
        }
    }
    std::sort(ts.begin(), ts.end());
    for (std::size_t i = 0; i + 1 < ts.size(); ++i) {
        const double tm = (ts[i] + ts[i + 1]) / 2.0;
        const PointD p{a.x + abx * tm, a.y + aby * tm};
        if (!in_region(polys, p)) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::vector<FillStitch> fill_tatami(const geometry::PathSet& region,
                                    const document::TatamiParams& params) {
    std::vector<FillStitch> out;
    const double spacing = static_cast<double>(std::max<std::int32_t>(1, params.row_spacing.value));
    const double stitchLen =
        static_cast<double>(std::max<std::int32_t>(1, params.stitch_length.value));
    const int stagger = std::max(1, params.stagger);

    const double cosA = std::cos(-params.angle.radians);
    const double sinA = std::sin(-params.angle.radians);
    const double cosB = std::cos(params.angle.radians);
    const double sinB = std::sin(params.angle.radians);

    // Polygones (extérieur + trous) exprimés dans le repère des rangées.
    std::vector<std::vector<PointD>> polys;
    const auto addPoly = [&](const geometry::Path& path) {
        std::vector<PointD> poly;
        poly.reserve(path.nodes.size());
        for (const auto& node : path.nodes) {
            poly.push_back(rotate({static_cast<double>(node.pos.x.value),
                                   static_cast<double>(node.pos.y.value)},
                                  cosA, sinA));
        }
        if (poly.size() >= 3) {
            polys.push_back(std::move(poly));
        }
    };
    addPoly(region.outer);
    for (const auto& hole : region.holes) {
        addPoly(hole);
    }
    if (polys.empty()) {
        return out;
    }

    double minY = polys[0][0].y;
    double maxY = minY;
    for (const auto& poly : polys) {
        for (const PointD& p : poly) {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }

    // 1) Construit les SEGMENTS de rangée (un par intervalle intérieur), avec
    //    leurs pénétrations d'aiguille. Les segments sont les nœuds d'un graphe.
    struct Segment {
        int row{0};
        double y{0.0};
        double lo{0.0};
        double hi{0.0};
        std::vector<double> pens;  // pénétrations, ordonnées lo -> hi
    };
    std::vector<Segment> segs;
    std::map<int, std::vector<int>> byRow;
    int rowIndex = 0;
    for (double y = minY + spacing / 2.0; y < maxY; y += spacing, ++rowIndex) {
        std::vector<double> xs;
        for (const auto& poly : polys) {
            scan_polygon(poly, y, xs);
        }
        std::sort(xs.begin(), xs.end());
        const double phase = stitchLen * (static_cast<double>(rowIndex % stagger) /
                                          static_cast<double>(stagger));
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
            const double lo = xs[i];
            const double hi = xs[i + 1];
            if (hi <= lo) {
                continue;
            }
            Segment s;
            s.row = rowIndex;
            s.y = y;
            s.lo = lo;
            s.hi = hi;
            s.pens.push_back(lo);
            const double firstK = std::ceil((lo - phase) / stitchLen);
            for (double x = phase + firstK * stitchLen; x < hi; x += stitchLen) {
                if (x > lo) {
                    s.pens.push_back(x);
                }
            }
            s.pens.push_back(hi);
            byRow[rowIndex].push_back(static_cast<int>(segs.size()));
            segs.push_back(std::move(s));
        }
    }
    const int n = static_cast<int>(segs.size());
    if (n == 0) {
        return out;
    }

    // 2) ADJACENCE : deux segments de rangées consécutives qui se chevauchent en
    //    x sont voisins — la bande entre eux est intérieure à la région, donc
    //    une liaison entre eux ne traverse jamais un trou (le trou aurait coupé
    //    la rangée). C'est ce qui permet de contourner les trous (§15.5).
    std::vector<std::vector<int>> adj(static_cast<std::size_t>(n));
    for (const auto& [row, ids] : byRow) {
        const auto it = byRow.find(row + 1);
        if (it == byRow.end()) {
            continue;
        }
        for (const int i : ids) {
            for (const int j : it->second) {
                const double ov = std::min(segs[i].hi, segs[j].hi) -
                                  std::max(segs[i].lo, segs[j].lo);
                if (ov > 0.0) {
                    adj[i].push_back(j);
                    adj[j].push_back(i);
                }
            }
        }
    }

    // 3) PARCOURS glouton du graphe : on suit toujours un voisin non visité (la
    //    liaison reste dans la région) ; à défaut, on saute (déplacement) vers le
    //    segment non visité le plus proche. On entre chaque segment par l'extrémité
    //    la plus proche du point courant. Déterministe (tie-break par index).
    const auto dist2 = [](PointD a, PointD b) {
        const double dx = a.x - b.x;
        const double dy = a.y - b.y;
        return dx * dx + dy * dy;
    };
    std::vector<int> order(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        order[static_cast<std::size_t>(i)] = i;
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (segs[a].row != segs[b].row) return segs[a].row < segs[b].row;
        return segs[a].lo < segs[b].lo;
    });

    std::vector<char> visited(static_cast<std::size_t>(n), 0);
    bool hasPrev = false;
    PointD prev{};
    int visitedCount = 0;
    int current = -1;
    bool jumpStart = true;  // true = on ARRIVE sur ce segment par un déplacement
    std::size_t orderCursor = 0;

    // Entrée (§15) : on démarre depuis ce point (repère des rangées) et l'on
    // choisit à chaque nouvelle composante le segment non visité le plus proche.
    const bool hasEntry = params.entry_point.has_value();
    if (hasEntry) {
        prev = rotate({static_cast<double>(params.entry_point->x.value),
                       static_cast<double>(params.entry_point->y.value)},
                      cosA, sinA);
        hasPrev = true;
    }
    // Cap du trajet cousu caché : au-delà, on saute (déplacement à découvert).
    const double underpathCap = std::max(6.0 * spacing, 8'000.0);

    // Autoroute d'underpath (§15) : contour extérieur RENTRÉ (repère des rangées).
    // Relier deux composantes en longeant ce contour reste intérieur et contourne
    // les trous (le contour encercle la région). Régions sans contour : pas
    // d'underpath (on saute).
    std::vector<PointD> hw;
    if (params.hidden_underpath) {
        if (auto r = geometry::inset_path_set(geometry::PathSet{region.outer, {}},
                                              params.underlay_inset);
            r && !r->empty() && r->front().outer.nodes.size() >= 3) {
            for (const auto& nd : r->front().outer.nodes) {
                hw.push_back(rotate({static_cast<double>(nd.pos.x.value),
                                     static_cast<double>(nd.pos.y.value)},
                                    cosA, sinA));
            }
        }
    }
    const auto seglen = [](PointD a, PointD b) {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
    };
    // Trajet le long de l'autoroute entre a et b : renvoie [a, sommets…, b] pour
    // la direction VALIDE (aucun segment ne coupe un bord/trou) la plus courte
    // sous `underpathCap`, sinon vide.
    const auto routeHighway = [&](PointD a, PointD b) -> std::vector<PointD> {
        const int H = static_cast<int>(hw.size());
        if (H < 3) {
            return {};
        }
        const auto nearest = [&](PointD p) {
            int bi = 0;
            double bd = std::numeric_limits<double>::max();
            for (int i = 0; i < H; ++i) {
                const double d = seglen(p, hw[static_cast<std::size_t>(i)]);
                if (d < bd) {
                    bd = d;
                    bi = i;
                }
            }
            return bi;
        };
        const int ia = nearest(a);
        const int ib = nearest(b);
        std::vector<PointD> bestPath;
        double bestLen = underpathCap;
        for (const bool fwd : {true, false}) {
            std::vector<PointD> v{a};
            int i = ia;
            for (int guard = 0; guard <= H; ++guard) {
                v.push_back(hw[static_cast<std::size_t>(i)]);
                if (i == ib) break;
                i = fwd ? (i + 1) % H : (i - 1 + H) % H;
            }
            v.push_back(b);
            double L = 0.0;
            bool ok = true;
            for (std::size_t k = 1; k < v.size(); ++k) {
                L += seglen(v[k - 1], v[k]);
                if (L > bestLen || connector_invalid(polys, v[k - 1], v[k])) {
                    ok = false;
                    break;
                }
            }
            if (ok && L <= bestLen) {
                bestLen = L;
                bestPath = std::move(v);
            }
        }
        return bestPath;
    };

    while (visitedCount < n) {
        if (current == -1) {
            if (hasEntry) {
                int best = -1;
                double bd = std::numeric_limits<double>::max();
                for (int j = 0; j < n; ++j) {
                    if (visited[static_cast<std::size_t>(j)]) continue;
                    const double d = std::min(dist2(prev, {segs[j].lo, segs[j].y}),
                                              dist2(prev, {segs[j].hi, segs[j].y}));
                    if (d < bd || (d == bd && (best == -1 || j < best))) {
                        bd = d;
                        best = j;
                    }
                }
                current = best;
            } else {
                while (orderCursor < order.size() &&
                       visited[static_cast<std::size_t>(order[orderCursor])]) {
                    ++orderCursor;
                }
                current = order[orderCursor];
            }
            jumpStart = true;  // segment atteint par saut (nouvelle composante)
        }
        const Segment& s = segs[static_cast<std::size_t>(current)];
        const PointD endLo{s.lo, s.y};
        const PointD endHi{s.hi, s.y};
        const bool forward = !hasPrev || dist2(prev, endLo) <= dist2(prev, endHi);
        const int m = static_cast<int>(s.pens.size());
        for (int k = 0; k < m; ++k) {
            const double x = forward ? s.pens[static_cast<std::size_t>(k)]
                                     : s.pens[static_cast<std::size_t>(m - 1 - k)];
            const PointD rp{x, s.y};
            // La liaison vers le premier point d'un segment est un SAUT (aiguille
            // levée) si l'on n'y est pas arrivé par une arête du graphe, OU si le
            // trajet cousu couperait un bord de la région ou d'un trou. Le
            // chevauchement des rangées ne donne que des candidats ; la validation
            // géométrique (connector_invalid) tranche.
            const bool cross = hasPrev && connector_invalid(polys, prev, rp);
            bool jump = (k == 0) && (jumpStart || !hasPrev || cross);
            // Underpath caché (§15) : au lieu d'un saut, on coud un trajet caché.
            // Émet des pénétrations intermédiaires taguées `travel` le long du
            // trajet, `rp` restant une pénétration normale. `emitTravel` échantillonne
            // une polyligne (repère des rangées) sans son dernier point (== rp).
            const auto emitTravel = [&](const std::vector<PointD>& poly) {
                for (std::size_t s = 1; s < poly.size(); ++s) {
                    const double d = seglen(poly[s - 1], poly[s]);
                    const int steps = std::max(1, static_cast<int>(std::ceil(d / stitchLen)));
                    const bool lastSeg = (s + 1 == poly.size());
                    for (int t = 1; t <= steps; ++t) {
                        if (lastSeg && t == steps) break;  // dernier point == rp
                        const double f = static_cast<double>(t) / steps;
                        const PointD ip{poly[s - 1].x + (poly[s].x - poly[s - 1].x) * f,
                                        poly[s - 1].y + (poly[s].y - poly[s - 1].y) * f};
                        out.push_back({to_um(rotate(ip, cosB, sinB)), false, true});
                    }
                }
            };
            if (jump && params.hidden_underpath && hasPrev) {
                // 1) trajet DIRECT s'il reste intérieur et court ;
                if (!cross && seglen(prev, rp) <= underpathCap) {
                    emitTravel({prev, rp});
                    jump = false;
                } else if (auto route = routeHighway(prev, rp); !route.empty()) {
                    // 2) sinon on longe le contour rentré (contourne les trous).
                    emitTravel(route);
                    jump = false;
                }
            }
            out.push_back({to_um(rotate(rp, cosB, sinB)), jump, false});
            prev = rp;
            hasPrev = true;
        }
        visited[static_cast<std::size_t>(current)] = 1;
        ++visitedCount;

        // Prochain voisin non visité, le plus proche d'une extrémité.
        int next = -1;
        double best = std::numeric_limits<double>::max();
        for (const int j : adj[static_cast<std::size_t>(current)]) {
            if (visited[static_cast<std::size_t>(j)]) {
                continue;
            }
            const double d = std::min(dist2(prev, {segs[j].lo, segs[j].y}),
                                      dist2(prev, {segs[j].hi, segs[j].y}));
            if (d < best || (d == best && (next == -1 || j < next))) {
                best = d;
                next = j;
            }
        }
        current = next;
        jumpStart = false;  // atteint par une arête du graphe -> couture
    }
    return out;
}

std::vector<std::vector<Vec2um>> tatami_underlay(const geometry::PathSet& region,
                                                 const document::TatamiParams& params) {
    std::vector<std::vector<Vec2um>> passes;

    // 1) Contour rentré : running le long du bord (et des trous), retrait
    //    `underlay_inset`. Politique sûre EXPLICITE : si un retrait est demandé
    //    (`underlay_inset` > 0) mais échoue ou fait disparaître la forme (pièce
    //    trop petite pour ce retrait), on N'ÉMET AUCUNE sous-couche de contour
    //    pour cette forme — jamais de repli silencieux sur le bord BRUT. Coudre
    //    exactement sur l'arête finale n'offrirait aucune marge de stabilisation
    //    et risquerait de déborder une fois la compensation de bord de la couche
    //    supérieure appliquée par-dessus. Un retrait NUL est une intention
    //    explicite distincte (pas un échec) : le bord brut est alors voulu.
    if (params.underlay_edge) {
        std::vector<geometry::PathSet> shells;
        if (params.underlay_inset.value > 0) {
            if (auto r = geometry::inset_path_set(region, params.underlay_inset); r && !r->empty()) {
                shells = std::move(*r);
            }
        } else {
            shells.push_back(region);
        }
        // On ne longe que le contour EXTÉRIEUR rentré : l'inset d'un trou le
        // rétrécit (vers l'intérieur du trou), si bien qu'en longer le bord
        // ferait passer la sous-couche AU-DESSUS du trou. Les bords de trous ne
        // reçoivent donc pas de sous-couche de contour (sûreté).
        for (const auto& shell : shells) {
            if (shell.outer.nodes.size() < 2) {
                continue;
            }
            auto pts = sample_path(shell.outer, params.stitch_length, Micrometers{500});
            if (pts.size() >= 2) {
                passes.push_back(std::move(pts));
            }
        }
    }

    // 2) Rangées perpendiculaires espacées : on réutilise le balayage tatami avec
    //    un angle à +90° et un grand pas, puis on découpe en trajets cousus (on
    //    ignore les sauts). Les options avancées sont neutralisées pour éviter
    //    toute récursion.
    if (params.underlay_parallel) {
        document::TatamiParams up = params;
        up.angle = Angle{params.angle.radians + std::numbers::pi / 2.0};
        up.row_spacing = Micrometers{std::max(params.underlay_spacing.value, params.row_spacing.value)};
        up.inset = Micrometers{0};
        up.underlay_edge = false;
        up.underlay_parallel = false;
        up.hidden_underpath = false;
        up.entry_point.reset();
        const auto fill = fill_tatami(region, up);
        std::vector<Vec2um> run;
        for (const auto& fs : fill) {
            if (fs.jump) {
                if (run.size() >= 2) passes.push_back(run);
                run.clear();
            }
            run.push_back(fs.pos);
        }
        if (run.size() >= 2) passes.push_back(std::move(run));
    }

    return passes;
}

bool segment_stays_in_region(const geometry::PathSet& region, Vec2um a, Vec2um b) {
    std::vector<std::vector<PointD>> polys;
    const auto addPoly = [&](const geometry::Path& path) {
        std::vector<PointD> poly;
        poly.reserve(path.nodes.size());
        for (const auto& node : path.nodes) {
            poly.push_back({static_cast<double>(node.pos.x.value),
                            static_cast<double>(node.pos.y.value)});
        }
        if (poly.size() >= 3) {
            polys.push_back(std::move(poly));
        }
    };
    addPoly(region.outer);
    for (const auto& hole : region.holes) {
        addPoly(hole);
    }
    if (polys.empty()) {
        return false;
    }
    const PointD pa{static_cast<double>(a.x.value), static_cast<double>(a.y.value)};
    const PointD pb{static_cast<double>(b.x.value), static_cast<double>(b.y.value)};
    return !connector_invalid(polys, pa, pb);
}

}  // namespace openstitch::stitch_generation
