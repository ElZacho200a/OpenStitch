// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/running_stitch.hpp"

#include <cmath>

namespace openstitch::stitch_generation {

namespace {

Vec2um lerp(Vec2um a, Vec2um b, double t) {
    return Vec2um{
        Micrometers{static_cast<std::int32_t>(
            std::lround(static_cast<double>(a.x.value) +
                        t * static_cast<double>(b.x.value - a.x.value)))},
        Micrometers{static_cast<std::int32_t>(
            std::lround(static_cast<double>(a.y.value) +
                        t * static_cast<double>(b.y.value - a.y.value)))}};
}

}  // namespace

std::vector<Vec2um> sample_path(const geometry::Path& path, Micrometers stitch_length,
                                Micrometers min_length) {
    std::vector<Vec2um> out;
    if (path.nodes.size() < 2 || stitch_length.value <= 0) {
        return out;
    }

    // Polyligne de travail (les tangentes/Béziers seront aplaties ici quand
    // elles existeront) ; un chemin fermé se termine à son point de départ.
    std::vector<Vec2um> poly;
    poly.reserve(path.nodes.size() + 1);
    for (const auto& node : path.nodes) {
        poly.push_back(node.pos);
    }
    if (path.closed) {
        poly.push_back(path.nodes.front().pos);
    }

    out.push_back(poly.front());
    for (std::size_t i = 1; i < poly.size(); ++i) {
        const Vec2um a = poly[i - 1];
        const Vec2um b = poly[i];
        const double len = length_um(b - a);
        if (len == 0.0) {
            continue;  // nœud dupliqué
        }
        const auto steps =
            std::max<std::int64_t>(1, static_cast<std::int64_t>(
                                          std::ceil(len / static_cast<double>(stitch_length.value))));
        for (std::int64_t s = 1; s <= steps; ++s) {
            const Vec2um p = lerp(a, b, static_cast<double>(s) / static_cast<double>(steps));
            const bool isNode = (s == steps);
            // Fusion des points intermédiaires trop proches ; les nœuds sont
            // conservés sauf s'ils coïncident avec le point précédent.
            const double fromLast = length_um(p - out.back());
            if (!isNode && fromLast < static_cast<double>(min_length.value)) {
                continue;
            }
            if (isNode && fromLast == 0.0) {
                continue;
            }
            out.push_back(p);
        }
    }
    return out;
}

std::vector<Vec2um> apply_repeats(const std::vector<Vec2um>& points, int repeats) {
    if (points.size() < 2 || repeats <= 1) {
        return points;
    }
    std::vector<Vec2um> out;
    if (repeats == 2) {
        // Aller-retour : le chemin complet puis son inverse.
        out.reserve(points.size() * 2 - 1);
        out = points;
        for (std::size_t i = points.size() - 1; i-- > 0;) {
            out.push_back(points[i]);
        }
        return out;
    }
    // Point triple (bean stitch) : chaque segment est cousu avant/arrière/avant.
    out.reserve((points.size() - 1) * 3 + 1);
    out.push_back(points[0]);
    for (std::size_t i = 1; i < points.size(); ++i) {
        out.push_back(points[i]);
        out.push_back(points[i - 1]);
        out.push_back(points[i]);
    }
    return out;
}

}  // namespace openstitch::stitch_generation
