// SPDX-License-Identifier: Apache-2.0
#include "lasso_select.hpp"

#include <algorithm>
#include <array>

namespace openstitch::desktop_v2 {

namespace {

using stitch_render::RectMm;

double cross(PointMm o, PointMm a, PointMm b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

bool onSegment(PointMm a, PointMm b, PointMm p) {
    return std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= p.y &&
           p.y <= std::max(a.y, b.y);
}

// Intersection de deux segments, extrémités et colinéarité comprises.
bool segmentsIntersect(PointMm a, PointMm b, PointMm c, PointMm d) {
    const double d1 = cross(a, b, c);
    const double d2 = cross(a, b, d);
    const double d3 = cross(c, d, a);
    const double d4 = cross(c, d, b);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
        return true;
    }
    return (d1 == 0 && onSegment(a, b, c)) || (d2 == 0 && onSegment(a, b, d)) ||
           (d3 == 0 && onSegment(c, d, a)) || (d4 == 0 && onSegment(c, d, b));
}

bool insideRect(PointMm p, const RectMm& box) {
    return p.x >= box.x0 && p.x <= box.x1 && p.y >= box.y0 && p.y <= box.y1;
}

std::array<PointMm, 4> corners(const RectMm& box) {
    return {PointMm{box.x0, box.y0}, PointMm{box.x1, box.y0}, PointMm{box.x1, box.y1},
            PointMm{box.x0, box.y1}};
}

// Au moins une arête du lasso touche la boîte.
bool edgeTouchesBox(const std::vector<PointMm>& polygon, const RectMm& box) {
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        if (segmentTouchesRect(polygon[i], polygon[(i + 1) % polygon.size()], box)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool pointInPolygon(PointMm p, const std::vector<PointMm>& polygon) {
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const PointMm a = polygon[i];
        const PointMm b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

bool segmentTouchesRect(PointMm a, PointMm b, const RectMm& box) {
    if (insideRect(a, box) || insideRect(b, box)) {
        return true;
    }
    const auto c = corners(box);
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (segmentsIntersect(a, b, c[i], c[(i + 1) % c.size()])) {
            return true;
        }
    }
    return false;
}

std::vector<std::uint64_t> lassoSelect(const std::vector<PointMm>& polygon,
                                       const std::map<std::uint64_t, RectMm>& boxes, LassoMode mode,
                                       const std::function<bool(std::uint64_t)>& accept) {
    std::vector<std::uint64_t> ids;
    if (polygon.size() < 3) {
        return ids;
    }
    for (const auto& [id, box] : boxes) { // std::map : ordre croissant
        if (accept && !accept(id)) {
            continue;
        }
        const auto c = corners(box);
        const auto cornerIn = [&](PointMm p) { return pointInPolygon(p, polygon); };
        bool hit = false;
        if (mode == LassoMode::Enclosed) {
            // Les quatre coins dedans, et aucune arête du lasso ne traverse la boîte : un lasso
            // en « C » dont l'ouverture passe dans la boîte ne l'englobe pas.
            hit = std::ranges::all_of(c, cornerIn) && !edgeTouchesBox(polygon, box);
        } else {
            hit = std::ranges::any_of(c, cornerIn) || edgeTouchesBox(polygon, box);
        }
        if (hit) {
            ids.push_back(id);
        }
    }
    return ids;
}

} // namespace openstitch::desktop_v2
