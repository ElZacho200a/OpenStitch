// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/geometry/clean.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

Path make_path(std::initializer_list<std::pair<std::int32_t, std::int32_t>> pts) {
    Path path;
    path.closed = true;
    for (const auto& [x, y] : pts) {
        path.nodes.push_back(PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, NodeType::Corner,
                                      std::nullopt, std::nullopt});
    }
    return path;
}

}  // namespace

TEST_CASE("clean : carre simple -> un PathSet sans trou") {
    const auto sets = clean_to_path_sets(
        {make_path({{0, 0}, {10'000, 0}, {10'000, 10'000}, {0, 10'000}})});
    REQUIRE(sets.has_value());
    REQUIRE(sets->size() == 1);
    CHECK((*sets)[0].outer.nodes.size() == 4);
    CHECK((*sets)[0].holes.empty());
}

TEST_CASE("clean : carre + trou -> hierarchie reconstruite") {
    const auto sets = clean_to_path_sets(
        {make_path({{0, 0}, {10'000, 0}, {10'000, 10'000}, {0, 10'000}}),
         make_path({{3'000, 3'000}, {7'000, 3'000}, {7'000, 7'000}, {3'000, 7'000}})});
    REQUIRE(sets.has_value());
    REQUIRE(sets->size() == 1);
    CHECK((*sets)[0].holes.size() == 1);
    // Aire du trou = 4x4 mm.
    CHECK(std::abs(signed_area_um2((*sets)[0].holes[0])) == 16'000'000.0);
}

TEST_CASE("clean : noeud papillon auto-intersecte -> decoupe proprement") {
    // Papillon : deux triangles reliés par un croisement en (5000, 5000).
    const auto sets = clean_to_path_sets(
        {make_path({{0, 0}, {10'000, 10'000}, {10'000, 0}, {0, 10'000}})});
    REQUIRE(sets.has_value());
    CHECK(sets->size() == 2);  // regle pair-impair : deux triangles distincts
    for (const auto& set : *sets) {
        CHECK(set.holes.empty());
        CHECK(set.outer.nodes.size() == 3);
    }
}

TEST_CASE("clean : deux formes disjointes -> deux PathSet") {
    const auto sets = clean_to_path_sets(
        {make_path({{0, 0}, {4'000, 0}, {4'000, 4'000}, {0, 4'000}}),
         make_path({{20'000, 0}, {24'000, 0}, {24'000, 4'000}, {20'000, 4'000}})});
    REQUIRE(sets.has_value());
    CHECK(sets->size() == 2);
}

TEST_CASE("clean : entree vide ou degeneree -> resultat vide sans erreur") {
    CHECK(clean_to_path_sets({}).value().empty());
    CHECK(clean_to_path_sets({make_path({{0, 0}, {1, 0}})}).value().empty());
}
