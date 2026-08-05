// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "openstitch/geometry/path.hpp"
#include "openstitch/geometry/polyline.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {
Vec2um um(std::int32_t x, std::int32_t y) { return Vec2um{Micrometers{x}, Micrometers{y}}; }
}  // namespace

TEST_CASE("insert_node_on_segment : segment droit, insertion au milieu exact") {
    Path rail;
    rail.closed = false;
    rail.nodes = {PathNode{um(0, 0), NodeType::Corner, {}, {}},
                 PathNode{um(10'000, 0), NodeType::Corner, {}, {}}};

    const Path out = insert_node_on_segment(rail, 0, 0.5);
    REQUIRE(out.nodes.size() == 3);
    CHECK(out.nodes[0].pos == um(0, 0));
    CHECK(out.nodes[1].pos == um(5'000, 0));
    CHECK(out.nodes[1].type == NodeType::Corner);
    CHECK(out.nodes[2].pos == um(10'000, 0));
}

TEST_CASE("insert_node_on_segment : segment courbe, subdivision De Casteljau exacte (forme conservee)") {
    Path rail;
    rail.closed = false;
    PathNode a{um(0, 0), NodeType::Smooth, {}, um(3'000, 0)};
    PathNode b{um(10'000, 0), NodeType::Smooth, um(-3'000, 2'000), {}};
    rail.nodes = {a, b};

    const auto before = flatten(rail, Micrometers{20});
    const Path split = insert_node_on_segment(rail, 0, 0.5);
    REQUIRE(split.nodes.size() == 3);
    // Le nouveau noeud est sur la courbe d'origine et tangente-continu.
    CHECK(split.nodes[1].type == NodeType::Smooth);
    CHECK(split.nodes[1].tan_in.has_value());
    CHECK(split.nodes[1].tan_out.has_value());

    const auto after = flatten(split, Micrometers{20});
    // Meme forme : aplatir les deux tronçons du chemin scinde doit redonner
    // (a la tolerance d'aplatissement pres) la MEME polyligne que l'original.
    REQUIRE(after.points.front() == before.points.front());
    REQUIRE(after.points.back() == before.points.back());
    CHECK(polyline_length(after.points) == Catch::Approx(polyline_length(before.points)).epsilon(0.01));
}

TEST_CASE("insert_node_on_segment : segment_index hors bornes renvoie le chemin inchange") {
    Path rail;
    rail.closed = false;
    rail.nodes = {PathNode{um(0, 0), NodeType::Corner, {}, {}},
                 PathNode{um(1'000, 0), NodeType::Corner, {}, {}}};
    const Path out = insert_node_on_segment(rail, 5, 0.5);
    CHECK(out.nodes.size() == 2);
    CHECK(out == rail);
}

TEST_CASE("insert_node_on_segment : chemin ferme, segment de fermeture (dernier -> premier)") {
    Path tri;
    tri.closed = true;
    tri.nodes = {PathNode{um(0, 0), NodeType::Corner, {}, {}},
                PathNode{um(10'000, 0), NodeType::Corner, {}, {}},
                PathNode{um(0, 10'000), NodeType::Corner, {}, {}}};
    const Path out = insert_node_on_segment(tri, 2, 0.5);  // segment noeud2 -> noeud0
    REQUIRE(out.nodes.size() == 4);
    CHECK(out.nodes[3].pos == um(0, 5'000));  // milieu de (0,10000)-(0,0)
    CHECK(out.closed);
}

TEST_CASE("polylines_cross : deux segments en X se croisent") {
    const std::vector<Vec2um> a{um(0, 0), um(10'000, 10'000)};
    const std::vector<Vec2um> b{um(0, 10'000), um(10'000, 0)};
    CHECK(polylines_cross(a, b));
}

TEST_CASE("polylines_cross : deux rails paralleles ne se croisent pas") {
    const std::vector<Vec2um> a{um(0, 0), um(10'000, 0)};
    const std::vector<Vec2um> b{um(0, 3'000), um(10'000, 3'000)};
    CHECK_FALSE(polylines_cross(a, b));
}

TEST_CASE("polylines_cross : extremite exactement partagee n'est pas un croisement") {
    const std::vector<Vec2um> a{um(0, 0), um(10'000, 0)};
    const std::vector<Vec2um> b{um(0, 0), um(0, 10'000)};
    CHECK_FALSE(polylines_cross(a, b));
}
