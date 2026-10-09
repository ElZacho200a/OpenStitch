// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/skeleton_satin.hpp"

#include <algorithm>
#include <cmath>
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
            for (const auto& smp : sampled.samples) {
                column.crossings.push_back({to_um(smp.a), to_um(smp.b)});
            }
        }
        if (!column.crossings.empty()) {
            std::vector<Vec2um> axisPts;
            axisPts.reserve(chainAxes[ci].points().size());
            for (const P2& q : chainAxes[ci].points()) {
                axisPts.push_back(to_um(q));
            }
            result.axes.push_back(std::move(axisPts));
            if (chains[ci].closed) {
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

} // namespace openstitch::auto_satin
