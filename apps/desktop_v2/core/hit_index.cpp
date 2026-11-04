// SPDX-License-Identifier: Apache-2.0
#include "hit_index.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::desktop_v2 {

std::array<std::uint8_t, 3> encode_id(std::uint64_t id) {
    return {static_cast<std::uint8_t>((id >> 16) & 0xFF),
            static_cast<std::uint8_t>((id >> 8) & 0xFF), static_cast<std::uint8_t>(id & 0xFF)};
}

std::uint64_t decode_id(const std::array<std::uint8_t, 3>& rgb) {
    return (static_cast<std::uint64_t>(rgb[0]) << 16) | (static_cast<std::uint64_t>(rgb[1]) << 8) |
           rgb[2];
}

namespace {
double distanceToSegment(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}
int cellOf(double v) {
    return static_cast<int>(std::floor(v / 4.0));
}
} // namespace

HitIndex HitIndex::build(const std::vector<stitch_render::ThreadSegment>& idSegments) {
    HitIndex index;
    index.segments_.reserve(idSegments.size());
    for (const auto& s : idSegments) {
        const std::uint64_t id = decode_id(s.rgb);
        const auto n = static_cast<std::uint32_t>(index.segments_.size());
        index.segments_.push_back({s.ax, s.ay, s.bx, s.by, id});
        const double x0 = std::min(s.ax, s.bx);
        const double x1 = std::max(s.ax, s.bx);
        const double y0 = std::min(s.ay, s.by);
        const double y1 = std::max(s.ay, s.by);
        for (int cx = cellOf(x0); cx <= cellOf(x1); ++cx) {
            for (int cy = cellOf(y0); cy <= cellOf(y1); ++cy) {
                index.grid_[{cx, cy}].push_back(n);
            }
        }
        index.lastSegment_[id] = n;
        auto [it, inserted] = index.bounds_.try_emplace(id, BoxMm{x0, y0, x1, y1});
        if (!inserted) {
            it->second.x0 = std::min(it->second.x0, x0);
            it->second.y0 = std::min(it->second.y0, y0);
            it->second.x1 = std::max(it->second.x1, x1);
            it->second.y1 = std::max(it->second.y1, y1);
        }
    }
    return index;
}

std::optional<std::uint64_t> HitIndex::pick(double xMm, double yMm, double tolMm) const {
    std::optional<std::uint32_t> best;
    const int r = std::max(1, static_cast<int>(std::ceil(tolMm / kCell)));
    for (int cx = cellOf(xMm) - r; cx <= cellOf(xMm) + r; ++cx) {
        for (int cy = cellOf(yMm) - r; cy <= cellOf(yMm) + r; ++cy) {
            const auto it = grid_.find({cx, cy});
            if (it == grid_.end()) {
                continue;
            }
            for (const std::uint32_t n : it->second) {
                const auto& s = segments_[n];
                // Plus grand indice = cousu plus tard = visuellement dessus.
                if ((!best || n > *best) &&
                    distanceToSegment(xMm, yMm, s.ax, s.ay, s.bx, s.by) <= tolMm) {
                    best = n;
                }
            }
        }
    }
    if (!best) {
        return std::nullopt;
    }
    return segments_[*best].id;
}

std::vector<std::uint64_t> HitIndex::inRect(const BoxMm& box) const {
    std::vector<std::uint64_t> out;
    for (const auto& [id, b] : bounds_) { // std::map : ordre d'identifiant croissant
        if (b.intersects(box)) {
            out.push_back(id);
        }
    }
    return out;
}

std::vector<std::uint64_t> HitIndex::inside(const BoxMm& box) const {
    std::vector<std::uint64_t> out;
    for (const auto& [id, b] : bounds_) {
        if (b.x0 >= box.x0 && b.y0 >= box.y0 && b.x1 <= box.x1 && b.y1 <= box.y1) {
            out.push_back(id);
        }
    }
    return out;
}

std::optional<std::pair<double, double>> HitIndex::samplePoint(std::uint64_t id) const {
    const auto it = lastSegment_.find(id);
    if (it == lastSegment_.end()) {
        return std::nullopt;
    }
    const auto& s = segments_[it->second];
    return std::pair<double, double>{(static_cast<double>(s.ax) + s.bx) / 2.0,
                                     (static_cast<double>(s.ay) + s.by) / 2.0};
}

std::optional<BoxMm> HitIndex::boundsOf(std::uint64_t id) const {
    const auto it = bounds_.find(id);
    if (it == bounds_.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace openstitch::desktop_v2
