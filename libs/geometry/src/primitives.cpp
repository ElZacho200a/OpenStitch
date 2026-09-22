// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/primitives.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "openstitch/geometry/simplify.hpp"

namespace openstitch::geometry {

namespace {

Micrometers um(double v) {
    return Micrometers{static_cast<std::int32_t>(std::lround(v))};
}

PathNode corner(Vec2um p) {
    return PathNode{p, NodeType::Corner, {}, {}};
}

} // namespace

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
    const double cx =
        (static_cast<double>(corner1.x.value) + static_cast<double>(corner2.x.value)) / 2.0;
    const double cy =
        (static_cast<double>(corner1.y.value) + static_cast<double>(corner2.y.value)) / 2.0;
    const double rx =
        std::abs(static_cast<double>(corner1.x.value) - static_cast<double>(corner2.x.value)) / 2.0;
    const double ry =
        std::abs(static_cast<double>(corner1.y.value) - static_cast<double>(corner2.y.value)) / 2.0;
    const double hx = kKappa * rx;
    const double hy = kKappa * ry;

    const Vec2um east{um(cx + rx), um(cy)};
    const Vec2um north{um(cx), um(cy + ry)};
    const Vec2um west{um(cx - rx), um(cy)};
    const Vec2um south{um(cx), um(cy - ry)};
    const Vec2um tanUpDown{Micrometers{0}, um(hy)};    // ± vertical
    const Vec2um tanRightLeft{um(hx), Micrometers{0}}; // ± horizontal

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

Path regular_polygon_path(Vec2um center, Micrometers radius, int sides, double start_angle) {
    if (sides < 3 || radius.value <= 0) {
        return Path{};
    }
    Path path;
    path.closed = true;
    path.nodes.reserve(static_cast<std::size_t>(sides));
    const double cx = static_cast<double>(center.x.value);
    const double cy = static_cast<double>(center.y.value);
    const double r = static_cast<double>(radius.value);
    for (int i = 0; i < sides; ++i) {
        const double a = start_angle + i * 2.0 * std::numbers::pi / sides;
        path.nodes.push_back(corner(Vec2um{um(cx + r * std::cos(a)), um(cy + r * std::sin(a))}));
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

Path smooth_open_path(const std::vector<Vec2um>& points) {
    std::vector<Vec2um> pts;
    for (const Vec2um& q : points) {
        if (pts.empty() || pts.back() != q) {
            pts.push_back(q);
        }
    }
    Path path;
    path.closed = false;
    if (pts.size() < 2) {
        return path;
    }
    const auto third = [](Vec2um from, Vec2um to, double k) {
        return Vec2um{um((to.x.value - from.x.value) * k), um((to.y.value - from.y.value) * k)};
    };
    const std::size_t n = pts.size();
    for (std::size_t i = 0; i < n; ++i) {
        PathNode node = corner(pts[i]);
        if (n >= 3) {
            if (i == 0) {
                node.tan_out = third(pts[0], pts[1], 1.0 / 3.0);
            } else if (i + 1 == n) {
                node.tan_in = third(pts[n - 1], pts[n - 2], 1.0 / 3.0);
            } else {
                // Tangente de Catmull-Rom (p_{i+1} - p_{i-1}) / 2, au tiers.
                const Vec2um t = third(pts[i - 1], pts[i + 1], 1.0 / 6.0);
                node.type = NodeType::Smooth;
                node.tan_out = t;
                node.tan_in = Vec2um{Micrometers{-t.x.value}, Micrometers{-t.y.value}};
            }
        }
        path.nodes.push_back(node);
    }
    return path;
}

} // namespace openstitch::geometry
