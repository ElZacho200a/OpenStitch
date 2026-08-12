// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "openstitch/geometry/boolean.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

Vec2um p(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

PathNode corner(Vec2um pos) {
    return PathNode{pos, NodeType::Corner, std::nullopt, std::nullopt};
}

Path square_path(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    Path path;
    path.closed = true;
    path.nodes = {corner(p(x0, y0)), corner(p(x1, y0)), corner(p(x1, y1)), corner(p(x0, y1))};
    return path;
}

double net_area_mm2(const PathSet& set) {
    double area = std::abs(signed_area_um2(set.outer)) / 1e6;
    for (const auto& hole : set.holes) area -= std::abs(signed_area_um2(hole)) / 1e6;
    return area;
}

}  // namespace

TEST_CASE("subtract_polygons : aucun cutout -> region inchangee") {
    const PathSet base{square_path(0, 0, 10'000, 10'000), {}};
    const auto result = subtract_polygons(base, {});
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(net_area_mm2((*result)[0]) == 100.0);
}

TEST_CASE("subtract_polygons : cutout entierement interieur -> trou net") {
    const PathSet base{square_path(0, 0, 10'000, 10'000), {}};
    const std::vector<Path> cutouts{square_path(3'000, 3'000, 7'000, 7'000)};
    const auto result = subtract_polygons(base, cutouts);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    REQUIRE((*result)[0].holes.size() == 1);
    CHECK(net_area_mm2((*result)[0]) == 84.0);  // 100 - 16 mm2
}

TEST_CASE("subtract_polygons : cutout couvrant toute la region -> resultat vide") {
    const PathSet base{square_path(1'000, 1'000, 9'000, 9'000), {}};
    const std::vector<Path> cutouts{square_path(0, 0, 10'000, 10'000)};
    const auto result = subtract_polygons(base, cutouts);
    REQUIRE(result.has_value());
    CHECK(result->empty());
}

// Deux cutouts adjacents, orientations OPPOSEES (un CCW, un CW) et qui se
// CHEVAUCHENT legerement (comme deux bandes de colonnes satin voisines
// partageant un barreau) : sous une regle pair-impair, le chevauchement
// s'annulerait et laisserait un trou parasite au milieu -- exactement le
// defaut que la regle NonZero doit eviter.
TEST_CASE("subtract_polygons : deux cutouts chevauchants, orientations opposees -> pas de trou parasite") {
    const PathSet base{square_path(0, 0, 10'000, 4'000), {}};
    Path left = square_path(0, 0, 5'500, 4'000);           // CCW (via corner() -> square_path deja CCW)
    Path right = square_path(4'500, 0, 10'000, 4'000);
    std::reverse(right.nodes.begin(), right.nodes.end());  // force CW : oriente a l'oppose
    const std::vector<Path> cutouts{left, right};
    const auto result = subtract_polygons(base, cutouts);
    REQUIRE(result.has_value());
    // Union des deux cutouts = la region entiere (0..10000) : rien ne doit
    // rester (ni trou residuel au centre, ni morceau non recouvert).
    CHECK(result->empty());
}

TEST_CASE("subtract_polygons : cutout hors de la region -> region inchangee") {
    const PathSet base{square_path(0, 0, 10'000, 10'000), {}};
    const std::vector<Path> cutouts{square_path(20'000, 20'000, 25'000, 25'000)};
    const auto result = subtract_polygons(base, cutouts);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(net_area_mm2((*result)[0]) == 100.0);
}

TEST_CASE("subtract_polygons : cutout degenere (< 3 sommets) -> ignore") {
    const PathSet base{square_path(0, 0, 10'000, 10'000), {}};
    Path degenerate;
    degenerate.closed = true;
    degenerate.nodes = {corner(p(1'000, 1'000)), corner(p(2'000, 2'000))};
    const auto result = subtract_polygons(base, {degenerate});
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(net_area_mm2((*result)[0]) == 100.0);
}

TEST_CASE("subtract_polygons : deterministe") {
    const PathSet base{square_path(0, 0, 10'000, 10'000), {}};
    const std::vector<Path> cutouts{square_path(3'000, 3'000, 7'000, 7'000)};
    const auto a = subtract_polygons(base, cutouts);
    const auto b = subtract_polygons(base, cutouts);
    REQUIRE((a.has_value() && b.has_value()));
    REQUIRE(a->size() == b->size());
    for (std::size_t i = 0; i < a->size(); ++i) {
        CHECK((*a)[i].outer.nodes == (*b)[i].outer.nodes);
    }
}
