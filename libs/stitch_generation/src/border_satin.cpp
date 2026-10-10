// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/border_satin.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::stitch_generation {

namespace {

struct V {
    double x{0.0};
    double y{0.0};
};

V operator+(V a, V b) {
    return {a.x + b.x, a.y + b.y};
}
V operator-(V a, V b) {
    return {a.x - b.x, a.y - b.y};
}
V operator*(V a, double s) {
    return {a.x * s, a.y * s};
}
double dot(V a, V b) {
    return a.x * b.x + a.y * b.y;
}
double cross(V a, V b) {
    return a.x * b.y - a.y * b.x;
}
double norm(V a) {
    return std::sqrt(dot(a, a));
}

V to_v(Vec2um p) {
    return {static_cast<double>(p.x.value), static_cast<double>(p.y.value)};
}
Vec2um to_um(V p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

constexpr double kMiterLimit = 3.0;                  // rapport onglet / décalage maximal
constexpr double kArcStep = std::numbers::pi / 15.0; // 12° entre deux barreaux d'un coin rond
constexpr double kStraightAngle = 0.017;             // ~1° : sommet quasi rectiligne

// Points d'un rail (décalage signé `dist` le long de la normale gauche) autour d'un sommet
// intérieur. `arcCount` > 0 : nombre de subdivisions de l'arc (coin rond, côté extérieur).
std::vector<V> offset_corner(V v, V n0, V n1, double cr, double dt, double len0, double len1,
                             double dist, bool round) {
    if (std::abs(dist) < 1e-9) {
        return {v};
    }
    const double d = 1.0 + dt; // 1 + cos(phi)
    const bool outer = dist * cr < 0.0;
    if (outer && std::abs(cr) > kStraightAngle) {
        if (round) {
            const double phi = std::atan2(cr, dt); // angle de virage signé
            const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(phi) / kArcStep)));
            std::vector<V> pts;
            pts.reserve(static_cast<std::size_t>(steps) + 1);
            const double a0 = std::atan2(n0.y, n0.x);
            for (int s = 0; s <= steps; ++s) {
                const double a = a0 + phi * static_cast<double>(s) / steps;
                pts.push_back(v + V{std::cos(a), std::sin(a)} * dist);
            }
            return pts;
        }
        if (d < 2.0 / (kMiterLimit * kMiterLimit)) { // onglet trop long : biseau
            return {v + n0 * dist, v + n1 * dist};
        }
    }
    V m = (n0 + n1) * (dist / std::max(d, 1e-9));
    if (!outer) {
        // Côté intérieur : l'onglet ne doit pas dépasser la moitié de l'arête voisine.
        const double t = std::abs(dist) * std::abs(cr) / std::max(d, 1e-9);
        const double lim = 0.5 * std::min(len0, len1);
        if (t > lim && t > 1e-9) {
            m = m * (lim / t);
        }
    }
    return {v + m};
}

} // namespace

Micrometers clamp_border_width(Micrometers width) {
    return Micrometers{std::clamp<std::int32_t>(width.value, 500, 20'000)};
}

bool ring_left_is_inside(const geometry::Path& ring, bool is_hole) {
    const bool ccw = geometry::signed_area_um2(ring) > 0.0;
    return ccw != is_hole;
}

std::optional<document::SatinParams> border_satin_from_path(const geometry::Path& path,
                                                            const document::BorderSatinSpec& spec,
                                                            Micrometers density,
                                                            bool left_is_inside) {
    const double width = static_cast<double>(clamp_border_width(spec.width).value);
    const bool round = spec.corner == document::BorderCorner::Round;
    const bool closed = path.closed;

    // Points distincts (écart > 20 µm) du contour aplati.
    std::vector<V> pts;
    for (const Vec2um& p : geometry::flatten(path, Micrometers{50}).points) {
        const V v = to_v(p);
        if (pts.empty() || norm(v - pts.back()) > 20.0) {
            pts.push_back(v);
        }
    }
    if (closed && pts.size() >= 2 && norm(pts.front() - pts.back()) <= 20.0) {
        pts.pop_back();
    }
    const std::size_t n = pts.size();
    if (n < 2 || (closed && n < 3)) {
        return std::nullopt;
    }

    // Distances signées des deux rails le long de la normale gauche (A le plus à gauche).
    double dA = 0.0;
    double dB = 0.0;
    switch (spec.side) {
    case document::BorderSide::Centered:
        dA = width / 2.0;
        dB = -width / 2.0;
        break;
    case document::BorderSide::Inside:
        (left_is_inside ? dA : dB) = left_is_inside ? width : -width;
        break;
    case document::BorderSide::Outside:
        (left_is_inside ? dB : dA) = left_is_inside ? -width : width;
        break;
    }

    const std::size_t edges = closed ? n : n - 1;
    std::vector<V> dir(edges);
    std::vector<V> nrm(edges);
    std::vector<double> len(edges);
    for (std::size_t e = 0; e < edges; ++e) {
        const V d = pts[(e + 1) % n] - pts[e];
        len[e] = norm(d);
        dir[e] = d * (1.0 / len[e]); // len > 20 µm garanti plus haut
        nrm[e] = V{-dir[e].y, dir[e].x};
    }

    struct Station {
        V a;
        V b;
    };
    std::vector<Station> stations;
    for (std::size_t i = 0; i < n; ++i) {
        const bool first = !closed && i == 0;
        const bool last = !closed && i + 1 == n;
        if (first || last) {
            const V nn = first ? nrm[0] : nrm[edges - 1];
            stations.push_back({pts[i] + nn * dA, pts[i] + nn * dB});
            continue;
        }
        const std::size_t e0 = (i + edges - 1) % edges;
        const std::size_t e1 = i % edges;
        const double cr = cross(dir[e0], dir[e1]);
        const double dt = dot(dir[e0], dir[e1]);
        auto pa = offset_corner(pts[i], nrm[e0], nrm[e1], cr, dt, len[e0], len[e1], dA, round);
        auto pb = offset_corner(pts[i], nrm[e0], nrm[e1], cr, dt, len[e0], len[e1], dB, round);
        const std::size_t k = std::max(pa.size(), pb.size());
        // Le rail à un seul point est répété : éventail de barreaux autour de ce point.
        for (std::size_t s = 0; s < k; ++s) {
            stations.push_back({pa[std::min(s, pa.size() - 1)], pb[std::min(s, pb.size() - 1)]});
        }
    }

    document::SatinParams params;
    params.density = density;
    params.border = spec;
    params.max_width = Micrometers{
        std::max<std::int32_t>(params.max_width.value, static_cast<std::int32_t>(width) + 1)};
    const auto addNode = [](geometry::Path& rail, V p) {
        const Vec2um u = to_um(p);
        if (rail.nodes.empty() || rail.nodes.back().pos != u) {
            rail.nodes.push_back({u, geometry::NodeType::Corner, std::nullopt, std::nullopt});
        }
    };
    params.rail_a.closed = false;
    params.rail_b.closed = false;
    for (const Station& s : stations) {
        addNode(params.rail_a, s.a);
        addNode(params.rail_b, s.b);
        const Vec2um ua = to_um(s.a);
        const Vec2um ub = to_um(s.b);
        if (params.rungs.empty() || params.rungs.back().a != ua || params.rungs.back().b != ub) {
            params.rungs.push_back({ua, ub, std::nullopt});
        }
    }
    if (closed) {
        // Fermeture : les rails reviennent EXACTEMENT à leur premier point (sans barreau
        // supplémentaire : les extrémités de rail sont des ancres du moteur satin).
        params.rail_a.nodes.push_back(params.rail_a.nodes.front());
        params.rail_b.nodes.push_back(params.rail_b.nodes.front());
    }
    if (params.rail_a.nodes.size() < 2 || params.rail_b.nodes.size() < 2 ||
        params.rungs.size() < 2) {
        return std::nullopt;
    }
    return params;
}

namespace {

// Satin de l'anneau `ring` (0 = extérieur, k = k-ième trou) du PathSet `path_set_index`.
std::optional<document::SatinParams> ring_satin(const geometry::PathSet& region,
                                                const document::BorderSatinSpec& spec,
                                                Micrometers density, std::uint32_t path_set_index,
                                                std::uint32_t ring) {
    const bool isHole = ring > 0;
    if (isHole && ring > region.holes.size()) {
        return std::nullopt;
    }
    const geometry::Path& path = isHole ? region.holes[ring - 1] : region.outer;
    auto p = border_satin_from_path(path, spec, density, ring_left_is_inside(path, isHole));
    if (p) {
        p->border->path_set = path_set_index;
        p->border->ring = ring;
    }
    return p;
}

} // namespace

std::vector<document::SatinParams> border_satin_from_region(const geometry::PathSet& region,
                                                            const document::BorderSatinSpec& spec,
                                                            Micrometers density,
                                                            std::uint32_t path_set_index) {
    std::vector<document::SatinParams> out;
    for (std::uint32_t ring = 0; ring <= region.holes.size(); ++ring) {
        if (auto p = ring_satin(region, spec, density, path_set_index, ring)) {
            out.push_back(std::move(*p));
        }
    }
    return out;
}

std::vector<document::SatinParams>
border_satin_from_paths(const std::vector<geometry::PathSet>& paths,
                        const document::BorderSatinSpec& spec, Micrometers density) {
    std::vector<document::SatinParams> out;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        auto part =
            border_satin_from_region(paths[i], spec, density, static_cast<std::uint32_t>(i));
        out.insert(out.end(), std::make_move_iterator(part.begin()),
                   std::make_move_iterator(part.end()));
    }
    return out;
}

std::optional<document::SatinParams>
regenerate_border_satin(const std::vector<geometry::PathSet>& paths,
                        const document::SatinParams& previous,
                        const document::BorderSatinSpec& spec) {
    if (!previous.border || previous.border->path_set >= paths.size()) {
        return std::nullopt;
    }
    auto fresh = ring_satin(paths[previous.border->path_set], spec, previous.density,
                            previous.border->path_set, previous.border->ring);
    if (!fresh) {
        return std::nullopt;
    }
    document::SatinParams out = previous; // conserve densité, sous-couches, verrous, entrées...
    out.rail_a = std::move(fresh->rail_a);
    out.rail_b = std::move(fresh->rail_b);
    out.rungs = std::move(fresh->rungs);
    out.border = fresh->border;
    out.max_width = Micrometers{std::max(previous.max_width.value, fresh->max_width.value)};
    // Le point d'entrée/sortie mémorisé n'a plus de sens sur une nouvelle géométrie.
    out.entry_point.reset();
    out.exit_point.reset();
    return out;
}

} // namespace openstitch::stitch_generation
