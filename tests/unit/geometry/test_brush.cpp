// SPDX-License-Identifier: Apache-2.0
// Pinceau de déformation du contour (HP-VEC-018).
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <numbers>

#include "openstitch/geometry/brush.hpp"
#include "openstitch/geometry/polyline.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

Vec2um pt(double x, double y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(y))}};
}

PathNode corner(double x, double y) {
    return PathNode{pt(x, y), NodeType::Corner, std::nullopt, std::nullopt};
}

Path circle(double radius, int samples) {
    Path p;
    p.closed = true;
    for (int i = 0; i < samples; ++i) {
        const double a = 2.0 * std::numbers::pi * i / samples;
        p.nodes.push_back(corner(radius * std::cos(a), radius * std::sin(a)));
    }
    return p;
}

BrushDab push(Vec2um center, double radius, Vec2um delta, double strength = 1.0) {
    BrushDab d;
    d.center = center;
    d.radius_um = radius;
    d.strength = strength;
    d.delta = delta;
    return d;
}

} // namespace

TEST_CASE("brush weight decays smoothly from the centre to the radius", "[geometry][brush]") {
    CHECK(brush_weight(0.0, 1000.0, 1.0) == 1.0);
    CHECK(brush_weight(1000.0, 1000.0, 1.0) == 0.0);
    CHECK(brush_weight(2000.0, 1000.0, 1.0) == 0.0);
    CHECK(brush_weight(250.0, 1000.0, 1.0) > brush_weight(500.0, 1000.0, 1.0));
    CHECK(brush_weight(500.0, 1000.0, 0.5) == 0.5 * brush_weight(500.0, 1000.0, 1.0));
    CHECK(brush_weight(0.0, 0.0, 1.0) == 0.0);
}

TEST_CASE("push moves the nodes under the brush and leaves the rest alone", "[geometry][brush]") {
    const Path c = circle(10'000.0, 72);
    const Path out = apply_brush_dab(c, push(pt(10'000, 0), 3'000.0, pt(1'200, 0)));
    // Un nœud proche du centre a été poussé vers l'extérieur (x croissant).
    bool movedOut = false;
    for (const PathNode& n : out.nodes) {
        if (n.pos.x.value > 10'000) {
            movedOut = true;
        }
    }
    CHECK(movedOut);
    // Les nœuds loin de la zone (côté opposé) sont strictement inchangés.
    CHECK(out.nodes.front().pos != Vec2um{});
    int unchanged = 0;
    for (const PathNode& n : out.nodes) {
        if (n.pos.x.value < -5'000) {
            for (const PathNode& o : c.nodes) {
                if (o.pos == n.pos) {
                    ++unchanged;
                    break;
                }
            }
        }
    }
    CHECK(unchanged > 10);
}

TEST_CASE("smooth mode flattens a bump", "[geometry][brush]") {
    Path p;
    p.closed = false;
    for (int i = 0; i <= 20; ++i) {
        p.nodes.push_back(corner(i * 500.0, i == 10 ? 800.0 : 0.0));
    }
    BrushDab d;
    d.center = pt(5'000, 800);
    d.radius_um = 2'000.0;
    d.strength = 1.0;
    d.mode = BrushMode::Smooth;
    const Path out = apply_brush_dab(p, d);
    std::int32_t maxY = 0;
    for (const PathNode& n : out.nodes) {
        maxY = std::max(maxY, n.pos.y.value);
    }
    CHECK(maxY < 800);
    // Extrémités d'un tracé ouvert fixes.
    CHECK(out.nodes.front().pos == p.nodes.front().pos);
    CHECK(out.nodes.back().pos == p.nodes.back().pos);
}

TEST_CASE("long segments under the brush are subdivided but the budget is bounded",
          "[geometry][brush]") {
    Path sq;
    sq.closed = true;
    sq.nodes = {corner(0, 0), corner(20'000, 0), corner(20'000, 20'000), corner(0, 20'000)};
    const Path out = apply_brush_dab(sq, push(pt(10'000, 0), 3'000.0, pt(0, 1'500)), 5);
    CHECK(out.nodes.size() > 4);
    CHECK(out.nodes.size() <= 4 + 5);
}

TEST_CASE("repeated strokes do not make the node count drift", "[geometry][brush]") {
    Path p = circle(10'000.0, 48);
    for (int i = 0; i < 200; ++i) {
        const double s = (i % 2 == 0) ? 600.0 : -600.0;
        p = apply_brush_dab(p, push(pt(10'000, 0), 3'000.0, pt(s, 0), 0.8));
    }
    // Aucune explosion : au plus quelques dizaines de nœuds au-dessus de l'original.
    CHECK(p.nodes.size() < 48 + 40);
    CHECK(p.nodes.size() >= 3);
}

TEST_CASE("collinear nodes in the zone are merged away", "[geometry][brush]") {
    Path p;
    p.closed = false;
    for (int i = 0; i <= 10; ++i) {
        p.nodes.push_back(corner(i * 1'000.0, 0));
    }
    BrushDab d = push(pt(5'000, 0), 3'000.0, pt(0, 0));
    d.strength = 0.5;
    const Path out = apply_brush_dab(p, d);
    CHECK(out.nodes.size() < p.nodes.size());
    CHECK(out.nodes.front().pos == p.nodes.front().pos);
    CHECK(out.nodes.back().pos == p.nodes.back().pos);
}

TEST_CASE("brush keeps tangents of curved nodes and is deterministic", "[geometry][brush]") {
    Path p;
    p.closed = true;
    p.nodes.push_back(PathNode{pt(0, 0), NodeType::Smooth, pt(-1'000, 0), pt(1'000, 0)});
    p.nodes.push_back(PathNode{pt(8'000, 0), NodeType::Smooth, pt(-1'000, 0), pt(1'000, 0)});
    p.nodes.push_back(PathNode{pt(8'000, 8'000), NodeType::Smooth, pt(0, -1'000), pt(0, 1'000)});
    const BrushDab d = push(pt(8'000, 0), 2'500.0, pt(500, 300));
    const Path a = apply_brush_dab(p, d);
    const Path b = apply_brush_dab(p, d);
    CHECK(a == b);
    for (const PathNode& n : a.nodes) {
        if (n.type == NodeType::Smooth) {
            CHECK(n.tan_in.has_value());
            CHECK(n.tan_out.has_value());
        }
    }
}

TEST_CASE("null brush parameters leave the path untouched", "[geometry][brush]") {
    const Path c = circle(5'000.0, 24);
    CHECK(apply_brush_dab(c, push(pt(5'000, 0), 0.0, pt(100, 0))) == c);
    CHECK(apply_brush_dab(c, push(pt(5'000, 0), 1'000.0, pt(100, 0), 0.0)) == c);
}

TEST_CASE("brush works on holes and validity rejects new crossings", "[geometry][brush]") {
    PathSet set;
    set.outer = circle(20'000.0, 64);
    set.holes.push_back(circle(5'000.0, 32));
    const PathSet out = apply_brush_dab(set, push(pt(5'000, 0), 2'000.0, pt(500, 0)));
    CHECK(out.holes.front() != set.holes.front());
    CHECK(out.outer == set.outer); // zone hors du contour extérieur
    CHECK(brush_result_is_valid(set, out, Micrometers{50}));
    // Pousser le trou à travers le contour extérieur est invalide.
    PathSet crossed = set;
    crossed.holes.front() =
        apply_brush_dab(set.holes.front(), push(pt(5'000, 0), 4'000.0, pt(30'000, 0)));
    CHECK_FALSE(brush_result_is_valid(set, crossed, Micrometers{50}));
}

TEST_CASE("a stroke over a dense contour stays fast", "[geometry][brush]") {
    const Path dense = circle(50'000.0, 4'000);
    Path p = dense;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        p = apply_brush_dab(p, push(pt(50'000, i * 20), 5'000.0, pt(0, 20)));
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    // 100 touches sur 4 000 nœuds : largement sous la seconde (build de test, même en Debug).
    CHECK(ms < 4'000);
    CHECK(p.nodes.size() <= dense.nodes.size());
}
