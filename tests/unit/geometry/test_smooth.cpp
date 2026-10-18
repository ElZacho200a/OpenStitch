// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "openstitch/geometry/curve_fit.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/geometry/smooth.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

Vec2um pt(double x, double y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(y))}};
}

PathNode corner_node(double x, double y) {
    return PathNode{pt(x, y), NodeType::Corner, std::nullopt, std::nullopt};
}

// Cercle fortement echantillonne (polygone dense a angles droits).
Path dense_circle(double radius, int samples) {
    Path p;
    p.closed = true;
    for (int i = 0; i < samples; ++i) {
        const double a = 2.0 * std::numbers::pi * i / samples;
        p.nodes.push_back(corner_node(radius * std::cos(a), radius * std::sin(a)));
    }
    return p;
}

// Arc ouvert dense (quart de cercle).
Path dense_arc(double radius, int samples) {
    Path p;
    p.closed = false;
    for (int i = 0; i <= samples; ++i) {
        const double a = 0.5 * std::numbers::pi * i / samples;
        p.nodes.push_back(corner_node(radius * std::cos(a), radius * std::sin(a)));
    }
    return p;
}

std::size_t curved_nodes(const Path& p) {
    std::size_t n = 0;
    for (const auto& nd : p.nodes) {
        n += (nd.tan_in || nd.tan_out) ? 1U : 0U;
    }
    return n;
}

} // namespace

TEST_CASE("smooth_path : reduit les noeuds d'un cercle dense dans la tolerance") {
    const Path circle = dense_circle(20'000.0, 400);
    const Micrometers tol{150};
    const Path smooth = smooth_path(circle, tol);
    CHECK(smooth.closed);
    CHECK(smooth.nodes.size() < circle.nodes.size() / 8);
    CHECK(curved_nodes(smooth) > 0);
    CHECK(hausdorff_distance_um(circle, smooth) <= 1.5 * tol.value);
}

TEST_CASE("smooth_path : conserve exactement les extremites d'un trace ouvert") {
    const Path arc = dense_arc(20'000.0, 200);
    const Path smooth = smooth_path(arc, Micrometers{150});
    REQUIRE(smooth.nodes.size() >= 2);
    CHECK_FALSE(smooth.closed);
    CHECK(smooth.nodes.front().pos == arc.nodes.front().pos);
    CHECK(smooth.nodes.back().pos == arc.nodes.back().pos);
    CHECK(smooth.nodes.size() < arc.nodes.size() / 4);
}

TEST_CASE("smooth_path : deterministe, identite hors domaine") {
    const Path circle = dense_circle(15'000.0, 300);
    CHECK(smooth_path(circle, Micrometers{120}) == smooth_path(circle, Micrometers{120}));
    CHECK(smooth_path(circle, Micrometers{0}) == circle);
    const Path tri = polygon_path({pt(0, 0), pt(1000, 0), pt(0, 1000)});
    CHECK(smooth_path(tri, Micrometers{100}) == tri);
}

TEST_CASE("smooth_path : un carre garde ses coins") {
    const Path square = rectangle_path(pt(0, 0), pt(20'000, 20'000));
    Path dense;
    dense.closed = true;
    for (int i = 0; i < 20; ++i) {
        dense.nodes.push_back(corner_node(i * 1000.0, 0));
    }
    for (int i = 0; i < 20; ++i) {
        dense.nodes.push_back(corner_node(20'000, i * 1000.0));
    }
    for (int i = 0; i < 20; ++i) {
        dense.nodes.push_back(corner_node(20'000 - i * 1000.0, 20'000));
    }
    for (int i = 0; i < 20; ++i) {
        dense.nodes.push_back(corner_node(0, 20'000 - i * 1000.0));
    }
    const Path smooth = smooth_path(dense, Micrometers{150});
    CHECK(smooth.nodes.size() <= 8);
    CHECK(hausdorff_distance_um(square, smooth) <= 200.0);
}

TEST_CASE("smooth_path_set : lisse exterieur et trous") {
    PathSet set;
    set.outer = dense_circle(20'000.0, 300);
    set.holes.push_back(dense_circle(5'000.0, 200));
    const PathSet smooth = smooth_path_set(set, Micrometers{120});
    CHECK(smooth.outer.nodes.size() < set.outer.nodes.size());
    REQUIRE(smooth.holes.size() == 1);
    CHECK(smooth.holes[0].nodes.size() < set.holes[0].nodes.size());
}
