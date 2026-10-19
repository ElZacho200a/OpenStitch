// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/node_transform.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "openstitch/geometry/reshape.hpp"

namespace openstitch::geometry {

namespace {

std::int32_t roundUm(double v) {
    return static_cast<std::int32_t>(std::lround(v));
}

struct Pt {
    double x;
    double y;
};

Pt toPt(Vec2um v) {
    return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
}

Vec2um toUm(Pt p) {
    return Vec2um{Micrometers{roundUm(p.x)}, Micrometers{roundUm(p.y)}};
}

// Échelle puis rotation d'un vecteur (sans translation).
Pt linear(const NodeTransform& t, Pt v, double c, double s) {
    return {t.scale * (c * v.x - s * v.y), t.scale * (s * v.x + c * v.y)};
}

} // namespace

Path transform_nodes(const Path& path, const std::vector<std::size_t>& selected,
                     const NodeTransform& transform, double soft_radius_um) {
    const std::size_t n = path.nodes.size();
    std::vector<bool> isSel(n, false);
    std::size_t count = 0;
    for (const std::size_t i : selected) {
        if (i < n && !isSel[i]) {
            isSel[i] = true;
            ++count;
        }
    }
    if (count == 0 || transform.is_identity()) {
        return path;
    }
    const double c = std::cos(transform.rotation_rad);
    const double s = std::sin(transform.rotation_rad);
    const Pt pivot = toPt(transform.pivot);
    const Pt shift = toPt(transform.translation);

    Path out = path;
    std::vector<Pt> disp(n, Pt{0.0, 0.0}); // déplacement des nœuds sélectionnés
    for (std::size_t i = 0; i < n; ++i) {
        if (!isSel[i]) {
            continue;
        }
        PathNode& node = out.nodes[i];
        const Pt p = toPt(node.pos);
        const Pt rel = linear(transform, {p.x - pivot.x, p.y - pivot.y}, c, s);
        const Pt moved{pivot.x + rel.x + shift.x, pivot.y + rel.y + shift.y};
        disp[i] = {moved.x - p.x, moved.y - p.y};
        node.pos = toUm(moved);
        if (node.tan_in) {
            node.tan_in = toUm(linear(transform, toPt(*node.tan_in), c, s));
        }
        if (node.tan_out) {
            node.tan_out = toUm(linear(transform, toPt(*node.tan_out), c, s));
        }
    }
    if (soft_radius_um > 0.0) {
        for (std::size_t i = 0; i < n; ++i) {
            if (isSel[i]) {
                continue;
            }
            const Pt p = toPt(path.nodes[i].pos);
            double best = std::numeric_limits<double>::max();
            std::size_t bestJ = n;
            for (std::size_t j = 0; j < n; ++j) {
                if (!isSel[j]) {
                    continue;
                }
                const Pt q = toPt(path.nodes[j].pos);
                const double d = std::hypot(p.x - q.x, p.y - q.y);
                if (d < best) { // égalité : plus petit indice (déterminisme)
                    best = d;
                    bestJ = j;
                }
            }
            if (bestJ == n || best >= soft_radius_um) {
                continue;
            }
            const double u = 1.0 - best / soft_radius_um;
            const double w = u * u * (3.0 - 2.0 * u);
            out.nodes[i].pos = toUm({p.x + disp[bestJ].x * w, p.y + disp[bestJ].y * w});
        }
    }
    return out;
}

std::optional<PathSet> transform_path_set_nodes(const PathSet& set, std::size_t path_index,
                                                const std::vector<std::size_t>& selected,
                                                const NodeTransform& transform,
                                                double soft_radius_um,
                                                Micrometers crossing_tolerance) {
    if (path_index > set.holes.size()) {
        return std::nullopt;
    }
    PathSet out = set;
    Path& target = path_index == 0 ? out.outer : out.holes[path_index - 1];
    target = transform_nodes(target, selected, transform, soft_radius_um);
    if (path_set_has_crossings(out, crossing_tolerance) &&
        !path_set_has_crossings(set, crossing_tolerance)) {
        return std::nullopt;
    }
    return out;
}

std::vector<std::size_t> nodes_in_rect(const Path& path, Vec2um min, Vec2um max) {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < path.nodes.size(); ++i) {
        const Vec2um p = path.nodes[i].pos;
        if (p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y) {
            result.push_back(i);
        }
    }
    return result;
}

} // namespace openstitch::geometry
