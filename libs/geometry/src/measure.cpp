// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/measure.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::geometry {

namespace {

double dx_of(Vec2um a, Vec2um b) {
    return static_cast<double>(b.x.value) - static_cast<double>(a.x.value);
}
double dy_of(Vec2um a, Vec2um b) {
    return static_cast<double>(b.y.value) - static_cast<double>(a.y.value);
}

} // namespace

SegmentMeasure measure_segment(Vec2um a, Vec2um b) {
    SegmentMeasure m;
    m.dx = Micrometers{b.x.value - a.x.value};
    m.dy = Micrometers{b.y.value - a.y.value};
    const double dx = dx_of(a, b);
    const double dy = dy_of(a, b);
    m.length_um = std::hypot(dx, dy);
    m.direction = Angle{m.is_degenerate() ? 0.0 : std::atan2(dy, dx)};
    return m;
}

double distance_um(Vec2um a, Vec2um b) {
    return std::hypot(dx_of(a, b), dy_of(a, b));
}

double polyline_length_um(const std::vector<Vec2um>& points, bool closed) {
    if (points.size() < 2) {
        return 0.0;
    }
    double total = polyline_length(points);
    if (closed && points.size() > 2) {
        total += distance_um(points.back(), points.front());
    }
    return total;
}

double path_length_um(const Path& path, Micrometers tolerance) {
    const Polyline flat = flatten(path, tolerance);
    return polyline_length_um(flat.points, flat.closed);
}

std::optional<Angle> angle_between(Vec2um vertex, Vec2um a, Vec2um b) {
    const double ax = dx_of(vertex, a);
    const double ay = dy_of(vertex, a);
    const double bx = dx_of(vertex, b);
    const double by = dy_of(vertex, b);
    if ((ax == 0.0 && ay == 0.0) || (bx == 0.0 && by == 0.0)) {
        return std::nullopt;
    }
    // atan2(|cross|, dot) est stable pour les angles proches de 0 et de pi.
    return Angle{std::atan2(std::abs(ax * by - ay * bx), ax * bx + ay * by)};
}

double polygon_area_um2(const std::vector<Vec2um>& points) {
    if (points.size() < 3) {
        return 0.0;
    }
    double twice = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vec2um& p = points[i];
        const Vec2um& q = points[(i + 1) % points.size()];
        twice += static_cast<double>(p.x.value) * static_cast<double>(q.y.value) -
                 static_cast<double>(q.x.value) * static_cast<double>(p.y.value);
    }
    return std::abs(twice) * 0.5;
}

double path_area_um2(const Path& path, Micrometers tolerance) {
    if (!path.closed || path.nodes.size() < 3) {
        return 0.0;
    }
    return polygon_area_um2(flatten(path, tolerance).points);
}

} // namespace openstitch::geometry
