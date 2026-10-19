// SPDX-License-Identifier: Apache-2.0
// Transformation groupée de nœuds (HP-VEC-017).
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "openstitch/geometry/node_transform.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

PathNode corner(std::int32_t x, std::int32_t y) {
    return PathNode{pt(x, y), NodeType::Corner, std::nullopt, std::nullopt};
}

// Polyligne ouverte horizontale de 7 nœuds espacés de 2 mm.
Path line7() {
    Path p;
    p.closed = false;
    for (int i = 0; i < 7; ++i) {
        p.nodes.push_back(corner(i * 2'000, 0));
    }
    return p;
}

} // namespace

TEST_CASE("translating selected nodes keeps their relative geometry", "[geometry][nodes]") {
    const Path p = line7();
    NodeTransform t;
    t.translation = pt(0, 1'500);
    const Path out = transform_nodes(p, {2, 3, 4}, t);
    REQUIRE(out.nodes.size() == 7);
    for (std::size_t i : {2u, 3u, 4u}) {
        CHECK(out.nodes[i].pos == pt(static_cast<std::int32_t>(i) * 2'000, 1'500));
    }
    for (std::size_t i : {0u, 1u, 5u, 6u}) {
        CHECK(out.nodes[i].pos == p.nodes[i].pos);
    }
}

TEST_CASE("soft transition drags neighbours with a decaying weight", "[geometry][nodes]") {
    const Path p = line7();
    NodeTransform t;
    t.translation = pt(0, 2'000);
    const Path out = transform_nodes(p, {3}, t, 5'000.0);
    CHECK(out.nodes[3].pos.y.value == 2'000);
    const auto dy = [&](std::size_t i) { return out.nodes[i].pos.y.value; };
    CHECK(dy(2) > 0);
    CHECK(dy(2) < 2'000);
    CHECK(dy(2) == dy(4));
    CHECK(dy(1) < dy(2));
    CHECK(dy(1) > 0);
    // Au-delà du rayon (6 mm) : immobile.
    CHECK(dy(0) == 0);
    CHECK(dy(6) == 0);
}

TEST_CASE("rotation turns positions and tangents about the pivot", "[geometry][nodes]") {
    Path p;
    p.closed = false;
    p.nodes.push_back(PathNode{pt(1'000, 0), NodeType::Smooth, pt(-100, 0), pt(100, 0)});
    p.nodes.push_back(corner(5'000, 0));
    NodeTransform t;
    t.pivot = pt(0, 0);
    t.rotation_rad = std::numbers::pi / 2.0;
    const Path out = transform_nodes(p, {0}, t);
    CHECK(out.nodes[0].pos == pt(0, 1'000));
    CHECK(out.nodes[0].tan_out == pt(0, 100));
    CHECK(out.nodes[0].tan_in == pt(0, -100));
    CHECK(out.nodes[1].pos == p.nodes[1].pos);
}

TEST_CASE("scaling about a pivot scales positions and tangents", "[geometry][nodes]") {
    Path p;
    p.closed = true;
    p.nodes = {corner(1'000, 1'000), corner(3'000, 1'000), corner(3'000, 3'000)};
    p.nodes[1].tan_out = pt(200, 0);
    NodeTransform t;
    t.pivot = pt(1'000, 1'000);
    t.scale = 2.0;
    const Path out = transform_nodes(p, {0, 1, 2}, t);
    CHECK(out.nodes[0].pos == pt(1'000, 1'000));
    CHECK(out.nodes[1].pos == pt(5'000, 1'000));
    CHECK(out.nodes[2].pos == pt(5'000, 5'000));
    CHECK(out.nodes[1].tan_out == pt(400, 0));
}

TEST_CASE("empty selection, identity and bad indices change nothing", "[geometry][nodes]") {
    const Path p = line7();
    NodeTransform t;
    t.translation = pt(10, 10);
    CHECK(transform_nodes(p, {}, t) == p);
    CHECK(transform_nodes(p, {99}, t) == p);
    CHECK(transform_nodes(p, {1, 2}, NodeTransform{}) == p);
}

TEST_CASE("transform is deterministic and keeps node count", "[geometry][nodes]") {
    const Path p = line7();
    NodeTransform t;
    t.translation = pt(300, -700);
    t.rotation_rad = 0.3;
    CHECK(transform_nodes(p, {1, 5}, t, 4'000.0) == transform_nodes(p, {5, 1, 1}, t, 4'000.0));
    CHECK(transform_nodes(p, {1, 5}, t, 4'000.0).nodes.size() == 7);
}

TEST_CASE("transform_path_set_nodes refuses crossings and bad targets", "[geometry][nodes]") {
    PathSet set;
    set.outer.closed = true;
    set.outer.nodes = {corner(0, 0), corner(10'000, 0), corner(10'000, 10'000), corner(0, 10'000)};
    NodeTransform t;
    t.translation = pt(0, 30'000);
    // Pousser un seul sommet du bas au-delà du haut croise le contour.
    CHECK_FALSE(transform_path_set_nodes(set, 0, {0}, t, 0.0, Micrometers{50}).has_value());
    CHECK_FALSE(transform_path_set_nodes(set, 4, {0}, t, 0.0, Micrometers{50}).has_value());
    NodeTransform ok;
    ok.translation = pt(500, 500);
    const auto moved = transform_path_set_nodes(set, 0, {0, 1}, ok, 0.0, Micrometers{50});
    REQUIRE(moved.has_value());
    CHECK(moved->outer.nodes[0].pos == pt(500, 500));
}

TEST_CASE("nodes_in_rect selects nodes inside the rectangle", "[geometry][nodes]") {
    const Path p = line7();
    const auto hit = nodes_in_rect(p, pt(1'000, -500), pt(7'000, 500));
    REQUIRE(hit.size() == 3);
    CHECK(hit[0] == 1);
    CHECK(hit[2] == 3);
    CHECK(nodes_in_rect(p, pt(100'000, 0), pt(200'000, 1)).empty());
}
