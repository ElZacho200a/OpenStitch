// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/to_curves.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace openstitch::geometry {

namespace {

Micrometers um(double v) {
    return Micrometers{static_cast<std::int32_t>(std::lround(v))};
}

double dx(Vec2um a, Vec2um b) {
    return static_cast<double>(b.x.value) - static_cast<double>(a.x.value);
}
double dy(Vec2um a, Vec2um b) {
    return static_cast<double>(b.y.value) - static_cast<double>(a.y.value);
}

} // namespace

Path convert_to_curves(const Path& path, const ToCurvesOptions& options) {
    const std::size_t n = path.nodes.size();
    if (n < 3) {
        return path;
    }
    const double threshold = options.corner_angle_deg * std::numbers::pi / 180.0;
    const double ratio = std::clamp(options.handle_ratio, 0.0, 0.5);

    Path out = path;
    for (std::size_t i = 0; i < n; ++i) {
        if (!path.closed && (i == 0 || i + 1 == n)) {
            continue;
        }
        const PathNode& node = path.nodes[i];
        if (node.tan_in || node.tan_out) {
            continue;
        }
        const Vec2um prev = path.nodes[(i + n - 1) % n].pos;
        const Vec2um next = path.nodes[(i + 1) % n].pos;
        const double inx = dx(prev, node.pos);
        const double iny = dy(prev, node.pos);
        const double outx = dx(node.pos, next);
        const double outy = dy(node.pos, next);
        const double len_in = std::hypot(inx, iny);
        const double len_out = std::hypot(outx, outy);
        if (len_in == 0.0 || len_out == 0.0) {
            continue;
        }
        const double turn = std::abs(std::atan2(inx * outy - iny * outx, inx * outx + iny * outy));
        if (turn > threshold) {
            continue;
        }
        const double cx = dx(prev, next);
        const double cy = dy(prev, next);
        const double clen = std::hypot(cx, cy);
        if (clen == 0.0) {
            continue; // demi-tour exact : pas de direction de corde
        }
        const double ux = cx / clen;
        const double uy = cy / clen;
        PathNode& dst = out.nodes[i];
        dst.type = NodeType::Smooth;
        dst.tan_out = Vec2um{um(ux * len_out * ratio), um(uy * len_out * ratio)};
        dst.tan_in = Vec2um{um(-ux * len_in * ratio), um(-uy * len_in * ratio)};
    }
    return out;
}

PathSet convert_to_curves(const PathSet& set, const ToCurvesOptions& options) {
    PathSet out;
    out.outer = convert_to_curves(set.outer, options);
    out.holes.reserve(set.holes.size());
    for (const Path& hole : set.holes) {
        out.holes.push_back(convert_to_curves(hole, options));
    }
    return out;
}

} // namespace openstitch::geometry
