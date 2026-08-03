// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/primitives.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/geometry/simplify.hpp"

namespace openstitch::geometry {

namespace {

Micrometers um(double v) {
    return Micrometers{static_cast<std::int32_t>(std::lround(v))};
}

PathNode corner(Vec2um p) {
    return PathNode{p, NodeType::Corner, {}, {}};
}

}  // namespace

Path rectangle_path(Vec2um corner1, Vec2um corner2) {
    const Micrometers left = std::min(corner1.x, corner2.x);
    const Micrometers right = std::max(corner1.x, corner2.x);
    const Micrometers bottom = std::min(corner1.y, corner2.y);
    const Micrometers top = std::max(corner1.y, corner2.y);
    Path path;
    path.closed = true;
    path.nodes = {corner({left, bottom}), corner({right, bottom}), corner({right, top}),
                 corner({left, top})};
    return path;
}

Path ellipse_path(Vec2um corner1, Vec2um corner2) {
    // Constante Bézier standard pour approximer un quart de cercle/ellipse
    // par une courbe cubique (erreur radiale relative maximale ~0,027 %).
    constexpr double kKappa = 0.5522847498307936;
    const double cx = (static_cast<double>(corner1.x.value) + static_cast<double>(corner2.x.value)) / 2.0;
    const double cy = (static_cast<double>(corner1.y.value) + static_cast<double>(corner2.y.value)) / 2.0;
    const double rx = std::abs(static_cast<double>(corner1.x.value) - static_cast<double>(corner2.x.value)) / 2.0;
    const double ry = std::abs(static_cast<double>(corner1.y.value) - static_cast<double>(corner2.y.value)) / 2.0;
    const double hx = kKappa * rx;
    const double hy = kKappa * ry;

    const Vec2um east{um(cx + rx), um(cy)};
    const Vec2um north{um(cx), um(cy + ry)};
    const Vec2um west{um(cx - rx), um(cy)};
    const Vec2um south{um(cx), um(cy - ry)};
    const Vec2um tanUpDown{Micrometers{0}, um(hy)};   // ± vertical
    const Vec2um tanRightLeft{um(hx), Micrometers{0}};  // ± horizontal

    Path path;
    path.closed = true;
    // Ordre antihoraire (repère Y vers le haut) : est -> nord -> ouest -> sud.
    path.nodes = {
        {east, NodeType::Smooth, Vec2um{} - tanUpDown, tanUpDown},
        {north, NodeType::Smooth, tanRightLeft, Vec2um{} - tanRightLeft},
        {west, NodeType::Smooth, tanUpDown, Vec2um{} - tanUpDown},
        {south, NodeType::Smooth, Vec2um{} - tanRightLeft, tanRightLeft},
    };
    return path;
}

Path polygon_path(const std::vector<Vec2um>& vertices) {
    Path path;
    if (vertices.size() < 3) {
        return path;
    }
    path.closed = true;
    path.nodes.reserve(vertices.size());
    for (const Vec2um& v : vertices) {
        path.nodes.push_back(corner(v));
    }
    return path;
}

Path freeform_path(const std::vector<Vec2um>& points, Micrometers tolerance) {
    if (points.size() < 3) {
        return Path{};
    }
    Path raw;
    raw.closed = true;
    raw.nodes.reserve(points.size());
    for (const Vec2um& p : points) {
        raw.nodes.push_back(corner(p));
    }
    Path result = simplify(raw, tolerance);
    if (result.nodes.size() < 3) {
        return Path{};
    }
    return result;
}

}  // namespace openstitch::geometry
