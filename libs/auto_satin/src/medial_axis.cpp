// SPDX-License-Identifier: Apache-2.0
// Axe médian d'une région polygonale par le diagramme de Voronoï de ses segments.
//
// Boost.Polygon (BSL-1.0) ne fournit que le diagramme. Tout le reste est ici : sélection des
// arêtes intérieures qui forment l'axe médian, échantillonnage des arcs paraboliques (sommet
// réflexe contre segment), rayon inscrit, retrait des rayons vers les coins convexes, fusion en
// graphe de jonctions/extrémités et détection des cycles sans noeud. Les types Boost ne sortent
// pas de ce fichier.
#include "medial_axis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>

#include <boost/polygon/polygon.hpp>
#include <boost/polygon/voronoi.hpp>

#include "geometry_detail.hpp"

namespace openstitch::auto_satin::detail {

namespace {

namespace bp = boost::polygon;
using VoronoiDiagram = bp::voronoi_diagram<double>;
using BoostSegment = bp::segment_data<int>;

constexpr std::size_t kMaxSegments = 6000;
// Sous ce rayon inscrit (µm), un sommet de Voronoï est SUR le contour : c'est le pied d'un rayon
// vers un coin convexe, pas un point de l'axe médian.
constexpr double kMinRadiusUm = 0.75;
constexpr double kParabolaToleranceUm = 8.0;
constexpr int kMaxParabolaSamples = 128;

struct Pt {
    double x{0.0};
    double y{0.0};
};

// Site d'une cellule : un point (extrémité de segment) ou un segment.
struct Site {
    bool is_point{false};
    Pt p0{};
    Pt p1{};
};

double distance_to(const Site& site, Pt v) {
    if (site.is_point) {
        return std::hypot(v.x - site.p0.x, v.y - site.p0.y);
    }
    const double dx = site.p1.x - site.p0.x;
    const double dy = site.p1.y - site.p0.y;
    const double len = std::hypot(dx, dy);
    if (len < 1e-9) {
        return std::hypot(v.x - site.p0.x, v.y - site.p0.y);
    }
    return std::abs(dx * (v.y - site.p0.y) - dy * (v.x - site.p0.x)) / len;
}

// --- Validation : le diagramme de segments exige des segments qui ne se coupent qu'à leurs
// extrémités communes. --------------------------------------------------------------------

std::int64_t orient(const BoostSegment& s, bp::point_data<int> c) {
    const std::int64_t ax = s.low().x();
    const std::int64_t ay = s.low().y();
    return (static_cast<std::int64_t>(s.high().x()) - ax) *
               (static_cast<std::int64_t>(c.y()) - ay) -
           (static_cast<std::int64_t>(s.high().y()) - ay) * (static_cast<std::int64_t>(c.x()) - ax);
}

int sign_of(std::int64_t v) {
    return v > 0 ? 1 : (v < 0 ? -1 : 0);
}

bool same(bp::point_data<int> a, bp::point_data<int> b) {
    return a.x() == b.x() && a.y() == b.y();
}

bool on_segment_bbox(const BoostSegment& s, bp::point_data<int> c) {
    return c.x() >= std::min(s.low().x(), s.high().x()) &&
           c.x() <= std::max(s.low().x(), s.high().x()) &&
           c.y() >= std::min(s.low().y(), s.high().y()) &&
           c.y() <= std::max(s.low().y(), s.high().y());
}

bool segments_conflict(const BoostSegment& a, const BoostSegment& b) {
    if (std::max(std::min(a.low().x(), a.high().x()), std::min(b.low().x(), b.high().x())) >
            std::min(std::max(a.low().x(), a.high().x()), std::max(b.low().x(), b.high().x())) ||
        std::max(std::min(a.low().y(), a.high().y()), std::min(b.low().y(), b.high().y())) >
            std::min(std::max(a.low().y(), a.high().y()), std::max(b.low().y(), b.high().y()))) {
        return false;
    }
    const int d1 = sign_of(orient(a, b.low()));
    const int d2 = sign_of(orient(a, b.high()));
    const int d3 = sign_of(orient(b, a.low()));
    const int d4 = sign_of(orient(b, a.high()));
    int shared = 0;
    bp::point_data<int> other_a = a.low();
    bp::point_data<int> other_b = b.low();
    for (const auto pa : {a.low(), a.high()}) {
        for (const auto pb : {b.low(), b.high()}) {
            if (same(pa, pb)) {
                ++shared;
                other_a = same(pa, a.low()) ? a.high() : a.low();
                other_b = same(pb, b.low()) ? b.high() : b.low();
            }
        }
    }
    if (shared >= 2) {
        return true; // segments confondus
    }
    if (shared == 1) {
        // Un seul sommet commun : conflit seulement si les segments se recouvrent (colinéaires,
        // du même côté du sommet).
        return d1 == 0 && d2 == 0 && (on_segment_bbox(a, other_b) || on_segment_bbox(b, other_a));
    }
    if (d1 * d2 < 0 && d3 * d4 < 0) {
        return true; // croisement franc
    }
    // Contact : une extrémité sur l'autre segment.
    return (d1 == 0 && on_segment_bbox(a, b.low())) || (d2 == 0 && on_segment_bbox(a, b.high())) ||
           (d3 == 0 && on_segment_bbox(b, a.low())) || (d4 == 0 && on_segment_bbox(b, a.high()));
}

// --- Arêtes de l'axe -------------------------------------------------------------------------

struct AxisEdge {
    int a{-1}; // noeud (après fusion des sommets confondus)
    int b{-1};
    std::vector<Vec2um> points;
    std::vector<double> radii;
};

Vec2um to_um(Pt p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

} // namespace

Result<MedialAxisGraph> build_medial_axis_graph(const geometry::PathSet& region) {
    // Segments du contour (extérieur et trous), sans doublons consécutifs.
    std::vector<BoostSegment> segments;
    const auto add_polygon = [&](const geometry::Path& path) {
        std::vector<bp::point_data<int>> pts;
        for (const auto& n : path.nodes) {
            const bp::point_data<int> p(static_cast<int>(n.pos.x.value),
                                        static_cast<int>(n.pos.y.value));
            if (pts.empty() || !same(pts.back(), p)) {
                pts.push_back(p);
            }
        }
        while (pts.size() > 1 && same(pts.front(), pts.back())) {
            pts.pop_back();
        }
        if (pts.size() < 3) {
            return;
        }
        for (std::size_t i = 0; i < pts.size(); ++i) {
            segments.emplace_back(pts[i], pts[(i + 1) % pts.size()]);
        }
    };
    add_polygon(region.outer);
    for (const auto& h : region.holes) {
        add_polygon(h);
    }
    MedialAxisGraph out;
    if (segments.empty()) {
        return out;
    }
    if (segments.size() > kMaxSegments) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "axe médian vectoriel : contour trop complexe (" +
                                         std::to_string(segments.size()) + " segments, maximum " +
                                         std::to_string(kMaxSegments) + ")"});
    }
    for (std::size_t i = 0; i < segments.size(); ++i) {
        for (std::size_t j = i + 1; j < segments.size(); ++j) {
            if (segments_conflict(segments[i], segments[j])) {
                return std::unexpected(
                    Error{ErrorCategory::OperationImpossible,
                          "axe médian vectoriel : le contour se coupe lui-même (segments " +
                              std::to_string(i) + " et " + std::to_string(j) + ")"});
            }
        }
    }

    VoronoiDiagram vd;
    bp::construct_voronoi(segments.begin(), segments.end(), &vd);

    const std::vector<Poly> polys = region_polys(region);
    const auto site_of = [&](const VoronoiDiagram::cell_type& cell) {
        const BoostSegment& seg = segments[cell.source_index()];
        Site s;
        const Pt lo{static_cast<double>(seg.low().x()), static_cast<double>(seg.low().y())};
        const Pt hi{static_cast<double>(seg.high().x()), static_cast<double>(seg.high().y())};
        switch (cell.source_category()) {
        case bp::SOURCE_CATEGORY_SEGMENT_START_POINT:
            s.is_point = true;
            s.p0 = lo;
            break;
        case bp::SOURCE_CATEGORY_SEGMENT_END_POINT:
            s.is_point = true;
            s.p0 = hi;
            break;
        default:
            s.is_point = false;
            s.p0 = lo;
            s.p1 = hi;
            break;
        }
        return s;
    };

    // Noeuds : sommets de Voronoï fusionnés à la grille du micromètre.
    std::map<std::pair<std::int32_t, std::int32_t>, int> nodeOf;
    std::vector<Vec2um> nodePos;
    std::vector<double> nodeRadius;
    const auto node_for = [&](Pt p, double radius) {
        const Vec2um q = to_um(p);
        const auto key = std::make_pair(q.x.value, q.y.value);
        const auto [it, inserted] = nodeOf.emplace(key, static_cast<int>(nodePos.size()));
        if (inserted) {
            nodePos.push_back(q);
            nodeRadius.push_back(radius);
        }
        return it->second;
    };

    std::vector<AxisEdge> axisEdges;
    for (const auto& e : vd.edges()) {
        if (!e.is_primary() || !e.is_finite() || &e > e.twin()) {
            continue; // chaque paire d'arêtes jumelles une seule fois
        }
        const Pt v0{e.vertex0()->x(), e.vertex0()->y()};
        const Pt v1{e.vertex1()->x(), e.vertex1()->y()};
        const Site siteA = site_of(*e.cell());
        const Site siteB = site_of(*e.twin()->cell());
        const double r0 = distance_to(siteA, v0);
        const double r1 = distance_to(siteA, v1);
        if (r0 < kMinRadiusUm || r1 < kMinRadiusUm) {
            continue; // rayon vers un coin convexe
        }

        std::vector<Pt> samples;
        std::vector<double> radii;
        bool parabola = false;
        if (e.is_curved() && siteA.is_point != siteB.is_point) {
            const Site& pointSite = siteA.is_point ? siteA : siteB;
            const Site& segSite = siteA.is_point ? siteB : siteA;
            const double dx = segSite.p1.x - segSite.p0.x;
            const double dy = segSite.p1.y - segSite.p0.y;
            const double len = std::hypot(dx, dy);
            if (len > 1e-9) {
                const Pt d{dx / len, dy / len};
                Pt nrm{-d.y, d.x};
                double fd = (pointSite.p0.x - segSite.p0.x) * nrm.x +
                            (pointSite.p0.y - segSite.p0.y) * nrm.y;
                if (fd < 0.0) {
                    nrm = {-nrm.x, -nrm.y};
                    fd = -fd;
                }
                if (fd > 1e-6) {
                    const double fx = (pointSite.p0.x - segSite.p0.x) * d.x +
                                      (pointSite.p0.y - segSite.p0.y) * d.y;
                    const auto xOf = [&](Pt v) {
                        return (v.x - segSite.p0.x) * d.x + (v.y - segSite.p0.y) * d.y;
                    };
                    const double x0 = xOf(v0);
                    const double x1 = xOf(v1);
                    const double maxStep = std::sqrt(8.0 * fd * kParabolaToleranceUm);
                    const int count = std::clamp(
                        static_cast<int>(std::ceil(std::abs(x1 - x0) / std::max(maxStep, 1.0))), 1,
                        kMaxParabolaSamples);
                    for (int i = 0; i <= count; ++i) {
                        Pt p = v0;
                        double r = r0;
                        if (i == count) {
                            p = v1;
                            r = r1;
                        } else if (i > 0) {
                            const double x = x0 + (x1 - x0) * i / count;
                            const double y = ((x - fx) * (x - fx) + fd * fd) / (2.0 * fd);
                            p = {segSite.p0.x + d.x * x + nrm.x * y,
                                 segSite.p0.y + d.y * x + nrm.y * y};
                            r = y;
                        }
                        samples.push_back(p);
                        radii.push_back(r);
                    }
                    parabola = true;
                }
            }
        }
        if (!parabola) {
            samples = {v0, v1};
            radii = {r0, r1};
        }

        const Pt mid = samples[samples.size() / 2];
        const Pt probe = parabola ? mid : Pt{(v0.x + v1.x) / 2.0, (v0.y + v1.y) / 2.0};
        if (!in_region(polys, P2{probe.x, probe.y})) {
            continue; // axe extérieur, ou dans un trou
        }

        AxisEdge edge;
        edge.a = node_for(v0, r0);
        edge.b = node_for(v1, r1);
        if (edge.a == edge.b) {
            continue; // arête réduite à un point par la fusion
        }
        for (std::size_t i = 0; i < samples.size(); ++i) {
            edge.points.push_back(to_um(samples[i]));
            edge.radii.push_back(radii[i]);
        }
        // Les extrémités reprennent la position fusionnée du noeud.
        edge.points.front() = nodePos[static_cast<std::size_t>(edge.a)];
        edge.points.back() = nodePos[static_cast<std::size_t>(edge.b)];
        axisEdges.push_back(std::move(edge));
    }

    const std::size_t nodeCount = nodePos.size();
    if (nodeCount == 0) {
        return out;
    }
    std::vector<int> degree(nodeCount, 0);
    for (const auto& e : axisEdges) {
        ++degree[static_cast<std::size_t>(e.a)];
        ++degree[static_cast<std::size_t>(e.b)];
    }

    // Composantes : celles dont tous les noeuds sont de degré 2 sont des cycles purs.
    std::vector<int> parent(nodeCount);
    std::iota(parent.begin(), parent.end(), 0);
    const std::function<int(int)> find = [&](int x) {
        while (parent[static_cast<std::size_t>(x)] != x) {
            parent[static_cast<std::size_t>(x)] =
                parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(x)])];
            x = parent[static_cast<std::size_t>(x)];
        }
        return x;
    };
    for (const auto& e : axisEdges) {
        parent[static_cast<std::size_t>(find(e.a))] = find(e.b);
    }
    std::vector<char> special(nodeCount, 0); // composante avec un noeud de degré != 2
    for (std::size_t i = 0; i < nodeCount; ++i) {
        if (degree[i] != 2) {
            special[static_cast<std::size_t>(find(static_cast<int>(i)))] = 1;
        }
    }

    // Cycles purs -> boucles fermées.
    std::vector<char> edgeUsed(axisEdges.size(), 0);
    std::vector<char> nodeInLoop(nodeCount, 0);
    std::vector<std::vector<std::size_t>> incident(nodeCount);
    for (std::size_t i = 0; i < axisEdges.size(); ++i) {
        incident[static_cast<std::size_t>(axisEdges[i].a)].push_back(i);
        incident[static_cast<std::size_t>(axisEdges[i].b)].push_back(i);
    }
    const auto lower = [&](int a, int b) {
        const Vec2um pa = nodePos[static_cast<std::size_t>(a)];
        const Vec2um pb = nodePos[static_cast<std::size_t>(b)];
        return std::tie(pa.y.value, pa.x.value) < std::tie(pb.y.value, pb.x.value);
    };
    std::vector<int> loopStarts;
    for (std::size_t i = 0; i < nodeCount; ++i) {
        const int root = find(static_cast<int>(i));
        if (special[static_cast<std::size_t>(root)]) {
            continue;
        }
        nodeInLoop[i] = 1;
    }
    std::map<int, int> startOfComponent; // racine -> noeud de départ canonique
    for (std::size_t i = 0; i < nodeCount; ++i) {
        if (!nodeInLoop[i]) {
            continue;
        }
        const int root = find(static_cast<int>(i));
        const auto it = startOfComponent.find(root);
        if (it == startOfComponent.end() || lower(static_cast<int>(i), it->second)) {
            startOfComponent[root] = static_cast<int>(i);
        }
    }
    for (const auto& [root, start] : startOfComponent) {
        (void)root;
        MedialAxisGraph::Loop loop;
        int cur = start;
        while (true) {
            std::size_t next = axisEdges.size();
            for (const std::size_t ei : incident[static_cast<std::size_t>(cur)]) {
                if (!edgeUsed[ei]) {
                    next = ei;
                    break;
                }
            }
            if (next == axisEdges.size()) {
                break;
            }
            edgeUsed[next] = 1;
            const AxisEdge& e = axisEdges[next];
            const bool forward = e.a == cur;
            const std::size_t n = e.points.size();
            for (std::size_t k = 0; k + 1 < n; ++k) { // sans répéter le dernier point
                const std::size_t idx = forward ? k : n - 1 - k;
                loop.points.push_back(e.points[idx]);
                loop.radii.push_back(e.radii[idx]);
            }
            cur = forward ? e.b : e.a;
            if (cur == start) {
                break;
            }
        }
        if (loop.points.size() >= 12) {
            out.loops.push_back(std::move(loop));
        }
    }

    // Graphe : noeuds hors boucles, triés par (y, x) pour un ordre canonique.
    std::vector<int> order;
    for (std::size_t i = 0; i < nodeCount; ++i) {
        if (!nodeInLoop[i]) {
            order.push_back(static_cast<int>(i));
        }
    }
    std::stable_sort(order.begin(), order.end(), lower);
    std::vector<std::uint32_t> idOf(nodeCount, 0);
    for (std::size_t k = 0; k < order.size(); ++k) {
        const int i = order[k];
        idOf[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(k);
        SkeletonNode node;
        node.id = static_cast<std::uint32_t>(k);
        node.position = nodePos[static_cast<std::size_t>(i)];
        const int deg = degree[static_cast<std::size_t>(i)];
        node.type = deg == 1   ? SkeletonNodeType::Endpoint
                    : deg == 2 ? SkeletonNodeType::Continuation
                               : SkeletonNodeType::Junction;
        node.local_radius_um = nodeRadius[static_cast<std::size_t>(i)];
        out.graph.nodes.push_back(node);
    }
    std::vector<std::size_t> edgeOrder;
    for (std::size_t i = 0; i < axisEdges.size(); ++i) {
        if (!edgeUsed[i] && !nodeInLoop[static_cast<std::size_t>(axisEdges[i].a)]) {
            edgeOrder.push_back(i);
        }
    }
    std::stable_sort(edgeOrder.begin(), edgeOrder.end(), [&](std::size_t x, std::size_t y) {
        const auto key = [&](std::size_t i) {
            const AxisEdge& e = axisEdges[i];
            return std::make_tuple(idOf[static_cast<std::size_t>(e.a)],
                                   idOf[static_cast<std::size_t>(e.b)], e.points.front().x.value,
                                   e.points.front().y.value, e.points.size());
        };
        return key(x) < key(y);
    });
    for (const std::size_t i : edgeOrder) {
        const AxisEdge& e = axisEdges[i];
        SkeletonEdge edge;
        edge.id = static_cast<std::uint32_t>(out.graph.edges.size());
        edge.from = idOf[static_cast<std::size_t>(e.a)];
        edge.to = idOf[static_cast<std::size_t>(e.b)];
        edge.centerline = e.points;
        edge.local_radii_um = e.radii;
        for (std::size_t k = 0; k + 1 < e.points.size(); ++k) {
            const double dx = static_cast<double>(e.points[k + 1].x.value - e.points[k].x.value);
            const double dy = static_cast<double>(e.points[k + 1].y.value - e.points[k].y.value);
            edge.length_um += std::hypot(dx, dy);
        }
        out.graph.edges.push_back(std::move(edge));
    }
    return out;
}

} // namespace openstitch::auto_satin::detail
