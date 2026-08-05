// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/path.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::geometry {

namespace {

Vec2um lerp(Vec2um a, Vec2um b, double t) {
    const double ax = static_cast<double>(a.x.value);
    const double ay = static_cast<double>(a.y.value);
    const double bx = static_cast<double>(b.x.value);
    const double by = static_cast<double>(b.y.value);
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(ax + (bx - ax) * t))},
                  Micrometers{static_cast<std::int32_t>(std::lround(ay + (by - ay) * t))}};
}

}  // namespace

Path insert_node_on_segment(const Path& path, std::size_t segment_index, double t) {
    const std::size_t n = path.nodes.size();
    if (n < 2) {
        return path;
    }
    const std::size_t edges = path.closed ? n : n - 1;
    if (segment_index >= edges) {
        return path;
    }
    t = std::clamp(t, 0.0, 1.0);

    const PathNode& a = path.nodes[segment_index];
    const PathNode& b = path.nodes[(segment_index + 1) % n];

    PathNode newA = a;
    PathNode newB = b;
    PathNode newMid;

    if (a.tan_out || b.tan_in) {
        // Points de contrôle absolus [P0,P1,P2,P3] de la cubique du segment.
        // Une tangente absente équivaut à un point de contrôle confondu avec
        // l'extrémité (dégénère proprement vers une cubique/quadratique).
        const Vec2um p0 = a.pos;
        const Vec2um p1 = a.tan_out ? a.pos + *a.tan_out : a.pos;
        const Vec2um p2 = b.tan_in ? b.pos + *b.tan_in : b.pos;
        const Vec2um p3 = b.pos;

        // De Casteljau à t : subdivision EXACTE, les deux moitiés recousent
        // une courbe identique au segment d'origine (aucune approximation).
        const Vec2um p01 = lerp(p0, p1, t);
        const Vec2um p12 = lerp(p1, p2, t);
        const Vec2um p23 = lerp(p2, p3, t);
        const Vec2um p012 = lerp(p01, p12, t);
        const Vec2um p123 = lerp(p12, p23, t);
        const Vec2um p0123 = lerp(p012, p123, t);

        newA.tan_out = a.tan_out ? std::optional<Vec2um>(p01 - p0) : std::nullopt;
        newB.tan_in = b.tan_in ? std::optional<Vec2um>(p23 - p3) : std::nullopt;
        // Tangente-continu par construction (propriété de De Casteljau) :
        // le nouveau nœud est donc Lisse, pas Coin.
        newMid = PathNode{p0123, NodeType::Smooth, p012 - p0123, p123 - p0123};
    } else {
        newMid = PathNode{lerp(a.pos, b.pos, t), NodeType::Corner, {}, {}};
    }

    Path out = path;
    out.nodes[segment_index] = newA;
    out.nodes[(segment_index + 1) % n] = newB;
    out.nodes.insert(out.nodes.begin() + static_cast<std::ptrdiff_t>(segment_index) + 1, newMid);
    return out;
}

}  // namespace openstitch::geometry
