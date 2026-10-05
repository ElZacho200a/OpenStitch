// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <string>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/autodigitize/contour_network.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/simplify.hpp"
#include "openstitch/vectorization/vectorize.hpp"

namespace openstitch::autodigitize {

std::size_t ContourComponent::junction_count() const {
    return static_cast<std::size_t>(
        std::count_if(nodes.begin(), nodes.end(),
                      [](const ContourNode& n) { return n.kind == ContourNodeKind::Junction; }));
}
std::size_t ContourComponent::endpoint_count() const {
    return static_cast<std::size_t>(
        std::count_if(nodes.begin(), nodes.end(),
                      [](const ContourNode& n) { return n.kind == ContourNodeKind::Endpoint; }));
}

namespace {

// Le defaut d'auto_satin (1500 px) perd les traits fins d'un grand dessin
// d'un seul tenant ; 4000 px garde ~90 um/pixel sur 360 mm.
constexpr int kContourRasterMaxDimension = 4000;
constexpr double kRadiansToDegrees = 180.0 / std::numbers::pi;

struct RawSeg {
    std::vector<Vec2um> pts;
    std::vector<double> radii; // rayon brut (distance transform), µm
    std::int32_t from{-1};
    std::int32_t to{-1};
    bool closed{false};
};

struct PixelRef {
    int col;
    int row;
};

PixelRef pixel_of(const auto_satin::RasterTransform& tr, Vec2um p) {
    return {static_cast<int>(std::lround(tr.col_of(p))),
            static_cast<int>(std::lround(tr.row_of(p)))};
}

// Boucles que le graphe de squelette ne porte pas (arete from==to rejetee ;
// anneau pur sans noeud) : on les retrouve dans le masque de squelette brut,
// en excluant les pixels du graphe BRUT (les branches elaguees ne ressuscitent
// donc pas).
struct Chain {
    std::vector<Vec2um> pts;
    std::vector<double> radii;
    bool cycle{false};
};

// Zhang-Suen laisse, au croisement de deux traits, un bloc 2x2 de pixels :
// chaque pixel y a un nombre de croisement de 2, donc build_skeleton_graph ne
// voit AUCUNE jonction et ses aretes traversent le croisement (aretes de
// plusieurs centaines de mm pour 50 mm reels sur une grille). On retire une
// diagonale du bloc : les deux pixels restants deviennent des pixels de
// jonction (cn >= 3, regroupes en un seul noeud). Le retrait n'est fait que
// s'il ne separe aucun voisin (connexite 8 locale preservee).
void repair_crossing_blocks(auto_satin::RasterMask& sk) {
    const int w = sk.width;
    const int h = sk.height;
    const auto at = [&](int x, int y) { return sk.at(x, y) != 0; };
    const auto set = [&](int x, int y, std::uint8_t v) {
        sk.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                  static_cast<std::size_t>(x)] = v;
    };
    // Etiquettes de composantes 8-connexes dans la fenetre 4x4 (x0..x0+3).
    const auto labels = [&](int x0, int y0, const std::array<bool, 4>& removed) {
        std::array<int, 16> lab{};
        lab.fill(-1);
        int next = 0;
        for (int i = 0; i < 16; ++i) {
            const int x = x0 + i % 4;
            const int y = y0 + i / 4;
            const bool inBlock = (i % 4 == 1 || i % 4 == 2) && (i / 4 == 1 || i / 4 == 2);
            bool on = at(x, y);
            if (inBlock) {
                const int bi = (i / 4 - 1) * 2 + (i % 4 - 1);
                on = on && !removed[static_cast<std::size_t>(bi)];
            }
            if (!on || lab[static_cast<std::size_t>(i)] >= 0) {
                continue;
            }
            std::vector<int> stack{i};
            lab[static_cast<std::size_t>(i)] = next;
            while (!stack.empty()) {
                const int c = stack.back();
                stack.pop_back();
                for (int d = 0; d < 16; ++d) {
                    if (lab[static_cast<std::size_t>(d)] >= 0 || std::abs(d % 4 - c % 4) > 1 ||
                        std::abs(d / 4 - c / 4) > 1) {
                        continue;
                    }
                    const int dx = x0 + d % 4;
                    const int dy = y0 + d / 4;
                    const bool dBlock = (d % 4 == 1 || d % 4 == 2) && (d / 4 == 1 || d / 4 == 2);
                    bool dOn = at(dx, dy);
                    if (dBlock) {
                        dOn = dOn &&
                              !removed[static_cast<std::size_t>((d / 4 - 1) * 2 + (d % 4 - 1))];
                    }
                    if (dOn) {
                        lab[static_cast<std::size_t>(d)] = next;
                        stack.push_back(d);
                    }
                }
            }
            ++next;
        }
        return lab;
    };
    const auto preserves = [&](int x0, int y0, const std::array<bool, 4>& removed) {
        const auto before = labels(x0, y0, {false, false, false, false});
        const auto after = labels(x0, y0, removed);
        for (int a = 0; a < 16; ++a) {
            const bool inBlockA = (a % 4 == 1 || a % 4 == 2) && (a / 4 == 1 || a / 4 == 2);
            if (inBlockA || before[static_cast<std::size_t>(a)] < 0) {
                continue;
            }
            for (int b = a + 1; b < 16; ++b) {
                const bool inBlockB = (b % 4 == 1 || b % 4 == 2) && (b / 4 == 1 || b / 4 == 2);
                if (inBlockB || before[static_cast<std::size_t>(b)] < 0) {
                    continue;
                }
                if (before[static_cast<std::size_t>(a)] == before[static_cast<std::size_t>(b)] &&
                    after[static_cast<std::size_t>(a)] != after[static_cast<std::size_t>(b)]) {
                    return false;
                }
            }
        }
        return true;
    };
    for (int y = 0; y + 1 < h; ++y) {
        for (int x = 0; x + 1 < w; ++x) {
            if (!(at(x, y) && at(x + 1, y) && at(x, y + 1) && at(x + 1, y + 1))) {
                continue;
            }
            // indices du bloc : 0 = (x,y) 1 = (x+1,y) 2 = (x,y+1) 3 = (x+1,y+1)
            const std::array<bool, 4> diagMain{true, false, false, true};
            const std::array<bool, 4> diagAnti{false, true, true, false};
            for (const auto& rem : {diagMain, diagAnti}) {
                if (preserves(x - 1, y - 1, rem)) {
                    if (rem[0]) {
                        set(x, y, 0);
                    }
                    if (rem[1]) {
                        set(x + 1, y, 0);
                    }
                    if (rem[2]) {
                        set(x, y + 1, 0);
                    }
                    if (rem[3]) {
                        set(x + 1, y + 1, 0);
                    }
                    break;
                }
            }
        }
    }
}

std::vector<Chain> residual_chains(const auto_satin::RasterMask& sk,
                                   const auto_satin::DistanceField& distance,
                                   const auto_satin::SkeletonGraph& rawGraph) {
    const int w = sk.width;
    const int h = sk.height;
    std::vector<Chain> out;
    if (w <= 0 || h <= 0) {
        return out;
    }
    const auto idx = [&](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
               static_cast<std::size_t>(x);
    };
    std::vector<std::uint8_t> covered(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    const auto mark = [&](std::vector<std::uint8_t>& m, Vec2um p, int rad) {
        const PixelRef r = pixel_of(sk.transform, p);
        for (int dy = -rad; dy <= rad; ++dy) {
            for (int dx = -rad; dx <= rad; ++dx) {
                const int x = r.col + dx;
                const int y = r.row + dy;
                if (x >= 0 && y >= 0 && x < w && y < h) {
                    m[idx(x, y)] = 1;
                }
            }
        }
    };
    for (const auto& e : rawGraph.edges) {
        for (const Vec2um p : e.centerline) {
            mark(covered, p, 0);
        }
    }
    for (const auto& n : rawGraph.nodes) {
        mark(covered, n.position, n.type == auto_satin::SkeletonNodeType::Junction ? 2 : 1);
    }
    auto_satin::RasterMask res = sk;
    bool any = false;
    for (std::size_t i = 0; i < res.pixels.size(); ++i) {
        if (covered[i]) {
            res.pixels[i] = 0;
        }
        any = any || res.pixels[i] != 0;
    }
    if (!any) {
        return out;
    }
    auto g = auto_satin::build_skeleton_graph(res, distance);

    // Anneaux purs : pixels du residu que le graphe n'a pas traces.
    std::vector<std::uint8_t> traced(covered.size(), 0);
    for (const auto& e : g.edges) {
        for (const Vec2um p : e.centerline) {
            mark(traced, p, 0);
        }
    }
    for (const auto& n : g.nodes) {
        mark(traced, n.position, 0);
    }
    std::vector<std::uint8_t> cutMark(covered.size(), 0);
    std::vector<std::uint8_t> seen(covered.size(), 0);
    bool cut = false;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!res.at(x, y) || traced[idx(x, y)] || seen[idx(x, y)]) {
                continue;
            }
            std::vector<std::size_t> comp;
            std::vector<std::pair<int, int>> stack{{x, y}};
            seen[idx(x, y)] = 1;
            while (!stack.empty()) {
                const auto [cx, cy] = stack.back();
                stack.pop_back();
                comp.push_back(idx(cx, cy));
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = cx + dx;
                        const int ny = cy + dy;
                        if ((dx != 0 || dy != 0) && res.at(nx, ny) && !traced[idx(nx, ny)] &&
                            !seen[idx(nx, ny)]) {
                            seen[idx(nx, ny)] = 1;
                            stack.push_back({nx, ny});
                        }
                    }
                }
            }
            if (comp.size() < 6) {
                continue;
            }
            for (const std::size_t c : comp) {
                cutMark[c] = 1;
            }
            res.pixels[idx(x, y)] = 0; // premier pixel en ordre de balayage : ouvre l'anneau
            cut = true;
        }
    }
    if (cut) {
        g = auto_satin::build_skeleton_graph(res, distance);
    }
    for (const auto& e : g.edges) {
        if (e.centerline.size() < 4) {
            continue;
        }
        Chain c;
        c.pts = e.centerline;
        c.radii = e.local_radii_um;
        const PixelRef r = pixel_of(sk.transform, c.pts.front());
        c.cycle = r.col >= 0 && r.row >= 0 && r.col < w && r.row < h && cutMark[idx(r.col, r.row)];
        out.push_back(std::move(c));
    }
    return out;
}

double quantile(std::vector<double> v, double q) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const double pos = q * static_cast<double>(v.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = static_cast<std::size_t>(std::ceil(pos));
    return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double>(lo));
}

// Virage maximal : angle entre la direction arrivant et celle repartant d'un
// sommet, mesurees sur une fenetre d'arc `win`.
double max_turn_deg(const std::vector<Vec2um>& pts, bool closed, double win) {
    if (pts.size() < 3) {
        return 0.0;
    }
    std::vector<Vec2um> p = pts;
    if (closed) {
        p.push_back(pts.front());
    }
    const auto cum = geometry::cumulative_lengths(p);
    const double total = cum.back();
    double best = 0.0;
    for (std::size_t i = 1; i + 1 < p.size(); ++i) {
        const double s = cum[i];
        if (s < win * 0.5 || total - s < win * 0.5) {
            continue; // trop pres d'un bout : mesure non fiable
        }
        const Vec2um a = geometry::point_at_length(p, cum, std::max(0.0, s - win));
        const Vec2um b = geometry::point_at_length(p, cum, std::min(total, s + win));
        const double ax = static_cast<double>(p[i].x.value - a.x.value);
        const double ay = static_cast<double>(p[i].y.value - a.y.value);
        const double bx = static_cast<double>(b.x.value - p[i].x.value);
        const double by = static_cast<double>(b.y.value - p[i].y.value);
        const double la = std::hypot(ax, ay);
        const double lb = std::hypot(bx, by);
        if (la < 1.0 || lb < 1.0) {
            continue;
        }
        const double c = std::clamp((ax * bx + ay * by) / (la * lb), -1.0, 1.0);
        best = std::max(best, std::acos(c) * kRadiansToDegrees);
    }
    return best;
}

ContourSegment finalize_segment(const RawSeg& raw, double pixel_um, Micrometers tolerance) {
    ContourSegment seg;
    seg.closed = raw.closed;
    seg.start_node = raw.from;
    seg.end_node = raw.to;

    // Largeurs : 2 * distance au fond - 1 pixel (un trait de W pixels a un
    // pic de (W+1)/2 pixels de distance).
    std::vector<double> widths;
    widths.reserve(raw.radii.size());
    for (const double r : raw.radii) {
        widths.push_back(std::max(0.0, 2.0 * r - pixel_um));
    }
    const std::size_t n = widths.size();
    const std::size_t trim = n >= 10 ? n / 5 : 0;
    std::vector<double> core(widths.begin() + static_cast<std::ptrdiff_t>(trim),
                             widths.end() - static_cast<std::ptrdiff_t>(trim));
    if (core.empty()) {
        core = widths;
    }
    double sum = 0.0;
    for (const double v : core) {
        sum += v;
    }
    seg.mean_width_um = core.empty() ? 0.0 : sum / static_cast<double>(core.size());
    seg.min_width_um = quantile(core, 0.1);
    seg.max_width_um = quantile(core, 0.9);

    geometry::Path path;
    path.closed = raw.closed;
    for (const Vec2um p : raw.pts) {
        path.nodes.push_back({p, geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    const geometry::Path simp = geometry::simplify(path, tolerance);
    std::size_t from = 0;
    for (const auto& nd : simp.nodes) {
        std::size_t found = raw.pts.size();
        for (std::size_t k = from; k < raw.pts.size(); ++k) {
            if (raw.pts[k] == nd.pos) {
                found = k;
                break;
            }
        }
        if (found == raw.pts.size()) {
            for (std::size_t k = 0; k < from && found == raw.pts.size(); ++k) {
                if (raw.pts[k] == nd.pos) {
                    found = k;
                }
            }
        }
        seg.centerline.push_back(nd.pos);
        if (found < raw.pts.size()) {
            from = found;
            seg.half_width_um.push_back(widths[found] / 2.0);
        } else {
            seg.half_width_um.push_back(seg.mean_width_um / 2.0);
        }
    }
    std::vector<Vec2um> measure = seg.centerline;
    seg.length_um = geometry::polyline_length(raw.closed ? [&] {
        auto c = measure;
        c.push_back(measure.front());
        return c;
    }()
                                                         : measure);
    seg.max_turn_deg =
        max_turn_deg(seg.centerline, raw.closed, std::max(1000.0, 1.5 * seg.mean_width_um));
    return seg;
}

double raw_length(const RawSeg& r) {
    return geometry::polyline_length(r.pts);
}

double median_of(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct NodeSet {
    std::vector<ContourNode> nodes;
    std::int32_t add(Vec2um p) {
        nodes.push_back({p, ContourNodeKind::Endpoint, 0});
        return static_cast<std::int32_t>(nodes.size() - 1);
    }
};

// Le squelette ne materialise pas toujours un noeud quand deux traits se
// croisent en diagonale (les deux aretes se traversent) : on coupe les deux
// aretes au point de contact et on cree la jonction.
void split_crossings(std::vector<RawSeg>& raws, NodeSet& ns, const auto_satin::RasterTransform& tr,
                     double px) {
    for (int iter = 0; iter < 64; ++iter) {
        std::map<std::pair<int, int>, std::pair<std::size_t, std::size_t>> occ;
        std::vector<std::pair<std::size_t, std::size_t>> range(raws.size());
        for (std::size_t s = 0; s < raws.size(); ++s) {
            const RawSeg& r = raws[s];
            const std::size_t n = r.pts.size();
            std::size_t a = 0;
            std::size_t b = n;
            if (!r.closed && n > 2) {
                const auto margin = [&](bool front) {
                    const double rad = front ? r.radii.front() : r.radii.back();
                    const double need = 1.5 * rad + 2.0 * px;
                    double acc = 0.0;
                    std::size_t k = 0;
                    while (k + 1 < n && acc < need) {
                        const std::size_t i = front ? k : n - 1 - k;
                        const std::size_t j = front ? k + 1 : n - 2 - k;
                        acc += length_um(r.pts[j] - r.pts[i]);
                        ++k;
                    }
                    return k;
                };
                a = margin(true);
                b = n - margin(false);
            }
            range[s] = {a, b};
            if (r.closed) {
                continue;
            }
            for (std::size_t i = a; i < b; ++i) {
                const PixelRef p = pixel_of(tr, r.pts[i]);
                occ.emplace(std::make_pair(p.col, p.row), std::make_pair(s, i));
            }
        }
        bool found = false;
        std::size_t s0 = 0, i0 = 0, t0 = 0, j0 = 0;
        for (std::size_t s = 0; s < raws.size() && !found; ++s) {
            if (raws[s].closed) {
                continue;
            }
            for (std::size_t i = range[s].first; i < range[s].second && !found; ++i) {
                const PixelRef p = pixel_of(tr, raws[s].pts[i]);
                for (int dy = -1; dy <= 1 && !found; ++dy) {
                    for (int dx = -1; dx <= 1 && !found; ++dx) {
                        const auto it = occ.find({p.col + dx, p.row + dy});
                        if (it != occ.end() && it->second.first > s) {
                            found = true;
                            s0 = s;
                            i0 = i;
                            t0 = it->second.first;
                            j0 = it->second.second;
                        }
                    }
                }
            }
        }
        if (!found) {
            return;
        }
        const Vec2um a = raws[s0].pts[i0];
        const Vec2um b = raws[t0].pts[j0];
        const Vec2um mid{Micrometers{(a.x.value + b.x.value) / 2},
                         Micrometers{(a.y.value + b.y.value) / 2}};
        const std::int32_t node = ns.add(mid);
        const auto split = [&](std::size_t si, std::size_t idx) {
            RawSeg tail;
            tail.pts.assign(raws[si].pts.begin() + static_cast<std::ptrdiff_t>(idx),
                            raws[si].pts.end());
            tail.radii.assign(raws[si].radii.begin() + static_cast<std::ptrdiff_t>(idx),
                              raws[si].radii.end());
            tail.to = raws[si].to;
            tail.from = node;
            tail.pts.front() = mid;
            raws[si].pts.resize(idx + 1);
            raws[si].radii.resize(idx + 1);
            raws[si].pts.back() = mid;
            raws[si].to = node;
            return tail;
        };
        RawSeg tailS = split(s0, i0);
        RawSeg tailT = split(t0, j0);
        raws.push_back(std::move(tailS));
        raws.push_back(std::move(tailT));
    }
}

// Deux noeuds (dont une jonction) separes par une arete plus courte que ~1,5
// largeur ne sont que les bords d'une meme confluence epaisse : on les
// fusionne.
void contract_junction_clusters(std::vector<RawSeg>& raws, NodeSet& ns) {
    for (int iter = 0; iter < 64; ++iter) {
        std::vector<int> deg(ns.nodes.size(), 0);
        for (const auto& r : raws) {
            if (r.from >= 0) {
                ++deg[static_cast<std::size_t>(r.from)];
            }
            if (r.to >= 0) {
                ++deg[static_cast<std::size_t>(r.to)];
            }
        }
        bool done = true;
        for (std::size_t k = 0; k < raws.size(); ++k) {
            const RawSeg& r = raws[k];
            if (r.closed || r.from < 0 || r.to < 0 || r.from == r.to) {
                continue;
            }
            const int d0 = deg[static_cast<std::size_t>(r.from)];
            const int d1 = deg[static_cast<std::size_t>(r.to)];
            if (std::min(d0, d1) < 2 || std::max(d0, d1) < 3) {
                continue; // un bout libre n'est jamais fusionne : ce serait un moignon
            }
            if (raw_length(r) >= 3.0 * median_of(r.radii)) {
                continue;
            }
            const std::int32_t keep = r.from;
            const std::int32_t gone = r.to;
            const Vec2um a = ns.nodes[static_cast<std::size_t>(keep)].position;
            const Vec2um b = ns.nodes[static_cast<std::size_t>(gone)].position;
            const Vec2um mid{Micrometers{(a.x.value + b.x.value) / 2},
                             Micrometers{(a.y.value + b.y.value) / 2}};
            ns.nodes[static_cast<std::size_t>(keep)].position = mid;
            raws.erase(raws.begin() + static_cast<std::ptrdiff_t>(k));
            for (auto& o : raws) {
                if (o.from == gone) {
                    o.from = keep;
                }
                if (o.to == gone) {
                    o.to = keep;
                }
            }
            for (auto& o : raws) {
                if (o.from == keep) {
                    o.pts.front() = mid;
                }
                if (o.to == keep) {
                    o.pts.back() = mid;
                }
            }
            done = false;
            break;
        }
        if (done) {
            return;
        }
    }
}

// Un trait qui change de regime de largeur (fin / satinable / trop large) est
// coupe a la transition : chaque morceau recoit sa propre strategie.
void split_width_regimes(std::vector<RawSeg>& raws, NodeSet& ns, double px,
                         const ContourLimits& lim) {
    const double minSatin = static_cast<double>(lim.min_satin_width.value);
    const double maxSatin = static_cast<double>(lim.max_satin_width.value);
    // Regimes bruts, sans hysteresis (elle rendait le nombre de segments non
    // monotone en `detail`) ; un regime plus court que kMinRun est absorbe.
    const auto regimes_of = [&](const std::vector<double>& med) {
        std::vector<int> out(med.size(), 1);
        for (std::size_t i = 0; i < med.size(); ++i) {
            out[i] = med[i] < minSatin ? 0 : (med[i] > maxSatin ? 2 : 1);
        }
        return out;
    };
    constexpr double kWindow = 500.0;  // µm de part et d'autre (mediane)
    constexpr double kMinRun = 6000.0; // µm : un regime plus court est absorbe
    const std::size_t original = raws.size();
    for (std::size_t k = 0; k < original; ++k) {
        RawSeg r = raws[k];
        if (r.closed && r.pts.size() >= 8) {
            // Anneau : on l'ouvre a un changement de regime (regimes calcules
            // sur l'anneau doublé, fenetre circulaire) ; sans changement il
            // reste un anneau unique.
            const std::size_t m = r.pts.size();
            std::vector<Vec2um> p2 = r.pts;
            p2.insert(p2.end(), r.pts.begin(), r.pts.end());
            std::vector<double> rad2 = r.radii;
            rad2.insert(rad2.end(), r.radii.begin(), r.radii.end());
            const auto c2 = geometry::cumulative_lengths(p2);
            std::vector<double> medArr(m, 0.0);
            for (std::size_t i = 0; i < m; ++i) {
                const std::size_t c = i + m / 2;
                std::vector<double> w;
                for (std::size_t j = c; j-- > 0 && c2[c] - c2[j] <= kWindow;) {
                    w.push_back(std::max(0.0, 2.0 * rad2[j] - px));
                }
                for (std::size_t j = c; j < p2.size() && c2[j] - c2[c] <= kWindow; ++j) {
                    w.push_back(std::max(0.0, 2.0 * rad2[j] - px));
                }
                medArr[i] = median_of(w);
            }
            const std::vector<int> reg = regimes_of(medArr);
            // reg[i] correspond au point (i + m/2) % m : on le reindexe.
            std::vector<int> regAt(m, 1);
            for (std::size_t i = 0; i < m; ++i) {
                regAt[(i + m / 2) % m] = reg[i];
            }
            std::size_t cut = m;
            for (std::size_t i = 0; i < m; ++i) {
                if (regAt[i] != regAt[(i + m - 1) % m]) {
                    cut = i;
                    break;
                }
            }
            if (cut == m) {
                continue;
            }
            std::rotate(r.pts.begin(), r.pts.begin() + static_cast<std::ptrdiff_t>(cut),
                        r.pts.end());
            std::rotate(r.radii.begin(), r.radii.begin() + static_cast<std::ptrdiff_t>(cut),
                        r.radii.end());
            r.pts.push_back(r.pts.front());
            r.radii.push_back(r.radii.front());
            r.closed = false;
            r.from = r.to = ns.add(r.pts.front());
        }
        const std::size_t n = r.pts.size();
        if (r.closed || n < 8) {
            continue;
        }
        const auto cum = geometry::cumulative_lengths(r.pts);
        std::vector<double> medArr(n, 0.0);
        std::size_t lo = 0;
        std::size_t hi = 0;
        for (std::size_t i = 0; i < n; ++i) {
            while (cum[i] - cum[lo] > kWindow) {
                ++lo;
            }
            while (hi + 1 < n && cum[hi + 1] - cum[i] <= kWindow) {
                ++hi;
            }
            std::vector<double> w;
            for (std::size_t j = lo; j <= hi; ++j) {
                w.push_back(std::max(0.0, 2.0 * r.radii[j] - px));
            }
            medArr[i] = median_of(w);
        }
        const std::vector<int> regime = regimes_of(medArr);
        // Runs, puis absorption des runs trop courts.
        struct Run {
            std::size_t begin, end; // [begin, end)
            int regime;
        };
        std::vector<Run> runs;
        for (std::size_t i = 0; i < n; ++i) {
            if (runs.empty() || runs.back().regime != regime[i]) {
                runs.push_back({i, i + 1, regime[i]});
            } else {
                runs.back().end = i + 1;
            }
        }
        for (bool changed = runs.size() > 1; changed;) {
            changed = false;
            for (std::size_t q = 0; q < runs.size() && runs.size() > 1; ++q) {
                const double len = cum[runs[q].end - 1] - cum[runs[q].begin];
                if (len >= kMinRun) {
                    continue;
                }
                const std::size_t into = q == 0 ? 1 : q - 1;
                runs[into].begin = std::min(runs[into].begin, runs[q].begin);
                runs[into].end = std::max(runs[into].end, runs[q].end);
                runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(q));
                // fusionne les voisins de meme regime
                for (std::size_t z = 0; z + 1 < runs.size();) {
                    if (runs[z].regime == runs[z + 1].regime) {
                        runs[z].end = runs[z + 1].end;
                        runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(z) + 1);
                    } else {
                        ++z;
                    }
                }
                changed = runs.size() > 1;
                break;
            }
        }
        if (runs.size() < 2) {
            continue;
        }
        std::vector<RawSeg> pieces;
        std::int32_t prevNode = r.from;
        for (std::size_t q = 0; q < runs.size(); ++q) {
            RawSeg piece;
            const std::size_t b = runs[q].begin;
            const std::size_t e = q + 1 < runs.size() ? runs[q].end : n;
            piece.pts.assign(r.pts.begin() + static_cast<std::ptrdiff_t>(b),
                             r.pts.begin() + static_cast<std::ptrdiff_t>(e));
            piece.radii.assign(r.radii.begin() + static_cast<std::ptrdiff_t>(b),
                               r.radii.begin() + static_cast<std::ptrdiff_t>(e));
            if (q + 1 < runs.size()) {
                // le point de coupe appartient aux deux morceaux
                piece.pts.push_back(r.pts[runs[q].end]);
                piece.radii.push_back(r.radii[runs[q].end]);
            }
            piece.from = prevNode;
            if (q + 1 < runs.size()) {
                prevNode = ns.add(piece.pts.back());
                piece.to = prevNode;
            } else {
                piece.to = r.to;
            }
            pieces.push_back(std::move(piece));
        }
        raws[k] = std::move(pieces.front());
        for (std::size_t q = 1; q < pieces.size(); ++q) {
            raws.push_back(std::move(pieces[q]));
        }
    }
}

std::optional<ContourComponent> analyze_piece(const geometry::PathSet& piece,
                                              std::array<std::uint8_t, 3> rgb,
                                              const ContourThresholds& th, const ContourLimits& lim,
                                              ContourNetwork& net, const std::string& label) {
    if (piece.outer.nodes.size() < 3) {
        return std::nullopt;
    }
    // Petits trous (poivre, anti-crenelage) : chacun creerait une boucle de
    // squelette. Un trou plus petit qu'une boucle minimale (aire d'un cercle de
    // perimetre `min_loop_perimeter`) est rebouche avant l'extraction.
    const double perimeter = static_cast<double>(th.min_loop_perimeter.value);
    const double minHoleArea = perimeter * perimeter / (4.0 * std::numbers::pi);
    // Le contour passe par les centres des pixels : une poche reliee a
    // l'exterieur par un pincement de largeur nulle est une ENTAILLE du contour
    // exterieur, pas un trou. Une dilatation de 5 um (sous le pixel de 50 um)
    // scelle ces pincements et en fait de vrais trous, filtrables par l'aire.
    geometry::PathSet sealed = piece;
    if (const auto grown = geometry::inset_path_set(piece, Micrometers{-5});
        grown && grown->size() == 1) {
        sealed = grown->front();
    }
    geometry::PathSet filtered = sealed;
    filtered.holes.clear();
    std::size_t droppedHoles = 0;
    for (const auto& hole : sealed.holes) {
        if (std::abs(geometry::signed_area_um2(hole)) < minHoleArea) {
            ++droppedHoles;
        } else {
            filtered.holes.push_back(hole);
        }
    }
    auto_satin::AutoSatinParameters ap;
    ap.cleanup.minimum_branch_length = th.min_branch_length;
    ap.raster.max_dimension = kContourRasterMaxDimension;
    ap.thresholds.min_satin_width = lim.min_satin_width;
    ap.thresholds.max_satin_width = lim.max_satin_width;
    const auto analysis = auto_satin::analyze_region(filtered, ap);
    if (!analysis) {
        ++net.failed_components;
        net.diagnostics.push_back(label + " : analyse du squelette impossible (" +
                                  analysis.error().message + ")");
        return std::nullopt;
    }
    const auto& dbg = analysis->debug;
    const double px = dbg.mask.transform.pixel_size_um;

    ContourComponent comp;
    comp.rgb = rgb;
    comp.region = filtered;
    comp.removed_small_loops += droppedHoles;
    comp.raster_pixel_um = px;
    auto_satin::RasterMask skeleton = dbg.skeleton;
    repair_crossing_blocks(skeleton);
    const auto rawGraph = auto_satin::build_skeleton_graph(skeleton, dbg.distance);
    const auto pruned = auto_satin::prune_graph(rawGraph, ap.cleanup);
    for (const auto& rb : pruned.removed) {
        if (rb.reason.rfind("branche terminale", 0) == 0) {
            ++comp.removed_short_branches;
        }
    }
    if (px > 75.0) {
        net.diagnostics.push_back(
            label + " : squelette calcule a " + std::to_string(static_cast<int>(px)) +
            " um/pixel (plafond de 1500 px) : les traits plus fins que ~" +
            std::to_string(static_cast<int>(3 * px / 100) / 10.0) + " mm peuvent etre perdus");
    }

    NodeSet ns;
    for (const auto& n : pruned.graph.nodes) {
        ns.add(n.position);
    }
    std::vector<RawSeg> raws;
    for (const auto& e : pruned.graph.edges) {
        RawSeg r;
        r.pts = e.centerline;
        r.radii = e.local_radii_um;
        r.from = static_cast<std::int32_t>(e.from);
        r.to = static_cast<std::int32_t>(e.to);
        raws.push_back(std::move(r));
    }

    const double attach = std::max(250.0, 5.0 * px);
    bool freshEndpoint = false; // dernier attach_node : extremite libre creee
    const auto attach_node = [&](Vec2um p) -> std::int32_t {
        std::int32_t best = -1;
        double bestD = attach;
        for (std::size_t i = 0; i < ns.nodes.size(); ++i) {
            const double d = length_um(ns.nodes[i].position - p);
            if (d < bestD) {
                bestD = d;
                best = static_cast<std::int32_t>(i);
            }
        }
        if (best >= 0) {
            return best;
        }
        for (const auto& rn : rawGraph.nodes) {
            if (rn.type == auto_satin::SkeletonNodeType::Junction &&
                length_um(rn.position - p) < attach * 1.5) {
                return ns.add(rn.position);
            }
        }
        freshEndpoint = true;
        return ns.add(p);
    };
    // Boucles residuelles : anneaux purs, ou boucles rattachees a UNE jonction
    // (les deux bouts au meme noeud). Le reste du residu est un artefact de
    // l'amas de pixels de jonction : ignore.
    for (auto& ch : residual_chains(skeleton, dbg.distance, rawGraph)) {
        RawSeg r;
        r.pts = std::move(ch.pts);
        r.radii = std::move(ch.radii);
        if (ch.cycle) {
            r.closed = true;
        } else {
            freshEndpoint = false;
            r.from = attach_node(r.pts.front());
            r.to = attach_node(r.pts.back());
            if (r.from != r.to && freshEndpoint) {
                // Bras libre perdu par le traceur, ou fragment colle a un trait
                // existant ? On ne garde que s'il s'en eloigne.
                const Vec2um mid = r.pts[r.pts.size() / 2];
                double nearest = 1e18;
                for (const auto& e : rawGraph.edges) {
                    for (const Vec2um q : e.centerline) {
                        nearest = std::min(nearest, length_um(q - mid));
                    }
                }
                if (nearest <= 4.0 * px) {
                    continue;
                }
            }
            // Bras que le traceur d'aretes a perdu entre deux jonctions (amas
            // de pixels) : garde s'il est assez long ; sinon artefact de l'amas.
            const double len = raw_length(r);
            if (r.from != r.to && len < 4.0 * median_of(r.radii) + 4.0 * px) {
                continue;
            }
            if (r.pts.front() != ns.nodes[static_cast<std::size_t>(r.from)].position) {
                r.pts.insert(r.pts.begin(), ns.nodes[static_cast<std::size_t>(r.from)].position);
                r.radii.insert(r.radii.begin(), r.radii.front());
            }
            if (r.pts.back() != ns.nodes[static_cast<std::size_t>(r.to)].position) {
                r.pts.push_back(ns.nodes[static_cast<std::size_t>(r.to)].position);
                r.radii.push_back(r.radii.back());
            }
        }
        raws.push_back(std::move(r));
    }

    split_crossings(raws, ns, skeleton.transform, px);
    contract_junction_clusters(raws, ns);
    split_width_regimes(raws, ns, px, lim);
    std::vector<ContourNode>& nodes = ns.nodes;

    for (const RawSeg& r : raws) {
        if (r.pts.size() < 2) {
            continue;
        }
        ContourSegment seg = finalize_segment(r, px, th.simplify_tolerance);
        // Boucle trop petite (anneau pur ou boucle rattachee a une jonction).
        const bool loop = r.closed || (r.from >= 0 && r.from == r.to);
        if (loop && seg.length_um < static_cast<double>(th.min_loop_perimeter.value)) {
            ++comp.removed_small_loops;
            continue;
        }
        if (r.from >= 0) {
            ++nodes[static_cast<std::size_t>(r.from)].degree;
        }
        if (r.to >= 0) {
            ++nodes[static_cast<std::size_t>(r.to)].degree;
        }
        comp.segments.push_back(std::move(seg));
    }
    // Reindexe les noeuds utilises et fixe leur type par degre.
    std::vector<std::int32_t> remap(nodes.size(), -1);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].degree > 0) {
            remap[i] = static_cast<std::int32_t>(comp.nodes.size());
            ContourNode n = nodes[i];
            n.kind = n.degree == 1   ? ContourNodeKind::Endpoint
                     : n.degree == 2 ? ContourNodeKind::Continuation
                                     : ContourNodeKind::Junction;
            comp.nodes.push_back(n);
        }
    }
    for (auto& s : comp.segments) {
        if (s.start_node >= 0) {
            s.start_node = remap[static_cast<std::size_t>(s.start_node)];
        }
        if (s.end_node >= 0) {
            s.end_node = remap[static_cast<std::size_t>(s.end_node)];
        }
    }
    if (comp.segments.empty()) {
        return std::nullopt;
    }
    return comp;
}

// Fermeture polygonale par groupe de couleur : les traits separes de moins de
// `distance` fusionnent (dilatation, union, erosion).
std::vector<geometry::PathSet> merge_near_pieces(const std::vector<geometry::PathSet>& pieces,
                                                 Micrometers distance) {
    const std::int32_t half = distance.value / 2;
    if (pieces.size() < 2 || half <= 0) {
        return pieces;
    }
    std::vector<geometry::PathSet> grown;
    for (const auto& p : pieces) {
        const auto g = geometry::inset_path_set(p, Micrometers{-half});
        if (!g) {
            return pieces;
        }
        grown.insert(grown.end(), g->begin(), g->end());
    }
    const auto u = geometry::union_polygons(grown);
    if (!u || u->size() >= pieces.size()) {
        return pieces; // rien a fusionner : geometrie d'origine intacte
    }
    std::vector<geometry::PathSet> out;
    for (const auto& s : *u) {
        const auto e = geometry::inset_path_set(s, Micrometers{half});
        if (!e) {
            return pieces;
        }
        out.insert(out.end(), e->begin(), e->end());
    }
    return out.empty() ? pieces : out;
}

} // namespace

Result<ContourNetwork> analyze_contours(const segmentation::Segmentation& seg,
                                        const ContourOptions& options) {
    if (options.mm_per_px.value <= 0.0) {
        return fail(ErrorCategory::UserInput, "Resolution de travail invalide");
    }
    ContourNetwork net;
    net.detail = std::isfinite(options.detail) ? std::clamp(options.detail, 0.0, 1.0) : 0.5;
    net.thresholds = contour_thresholds(net.detail);
    const ContourLimits lim = contour_limits();

    std::size_t largestSlot = 0;
    std::size_t largestCount = 0;
    bool anyRegion = false;
    for (std::size_t s = 0; s < seg.region_slots.size(); ++s) {
        if (seg.region_slots[s]) {
            anyRegion = true;
            if (seg.region_slots[s]->pixel_count > largestCount) {
                largestCount = seg.region_slots[s]->pixel_count;
                largestSlot = s;
            }
        }
    }
    if (!anyRegion) {
        return fail(ErrorCategory::UserInput, "Aucune region a numeriser");
    }
    const std::optional<std::array<std::uint8_t, 3>> background =
        options.skip_largest_region ? std::optional{seg.region_slots[largestSlot]->rgb}
                                    : std::nullopt;

    // Vectorisation a tolerance FIXE (pas DST) : la geometrie ne depend pas de
    // `detail`, qui ne pilote que les seuils de nettoyage (monotonie des
    // comptes de segments) et la simplification des lignes mediennes.
    const Micrometers vecTol{200};
    const vectorization::VectorizeOptions vecOpts{options.mm_per_px, vecTol};

    std::map<std::array<std::uint8_t, 3>, std::vector<geometry::PathSet>> byColour;
    for (const auto& slot : seg.region_slots) {
        if (!slot || (background && slot->rgb == *background)) {
            continue;
        }
        auto sets = vectorization::vectorize_region(seg, slot->id, vecOpts);
        if (!sets || sets->empty()) {
            // Typiquement un trait de 1-2 px : le contour passe par les centres
            // des pixels et n'a aucune aire. Jamais perdu en silence.
            ++net.failed_components;
            net.diagnostics.push_back(
                "Region " + std::to_string(slot->id.value) + " (" +
                std::to_string(slot->pixel_count) +
                " px) : trait trop fin pour etre vectorise (1-2 px de large) : non brode");
            continue;
        }
        auto& dst = byColour[slot->rgb];
        dst.insert(dst.end(), sets->begin(), sets->end());
    }
    if (byColour.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Aucune region exploitable");
    }

    const auto_satin::SkeletonCacheScope cache;
    std::size_t counter = 0;
    for (auto& [rgb, pieces] : byColour) {
        const auto merged = merge_near_pieces(pieces, net.thresholds.merge_distance);
        for (const auto& piece : merged) {
            ++counter;
            const std::size_t failedBefore = net.failed_components;
            auto comp = analyze_piece(piece, rgb, net.thresholds, lim, net,
                                      "Trait " + std::to_string(counter));
            if (!comp) {
                if (net.failed_components != failedBefore) {
                    continue; // deja diagnostique par analyze_piece
                }
                // Sans ligne mediane (point, disque) : element isole sous le seuil.
                ++net.removed_isolated;
                net.diagnostics.push_back("Trait " + std::to_string(counter) +
                                          " : aucune ligne mediane exploitable (point, disque ou "
                                          "trait trop fin) : ignore");
                continue;
            }
            const bool hasJunction = comp->junction_count() > 0;
            double total = 0.0;
            for (const auto& s : comp->segments) {
                total += s.length_um;
            }
            if (!hasJunction && comp->segments.size() == 1 && !comp->segments[0].closed &&
                total < static_cast<double>(net.thresholds.min_isolated_length.value)) {
                ++net.removed_isolated;
                net.diagnostics.push_back("Trait " + std::to_string(counter) +
                                          " : element isole de " +
                                          std::to_string(static_cast<int>(total / 100.0) / 10.0) +
                                          " mm sous le seuil de detail : ignore");
                continue;
            }
            net.components.push_back(std::move(*comp));
        }
    }
    return net;
}

} // namespace openstitch::autodigitize
