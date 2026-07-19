// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/geometry/offset.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

PathSet square(std::int32_t s) {
    Path p;
    p.closed = true;
    p.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{s}, Micrometers{0}}, NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{s}, Micrometers{s}}, NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{0}, Micrometers{s}}, NodeType::Corner, {}, {}},
    };
    return {p, {}};
}

}  // namespace

TEST_CASE("inset : retrait interieur reduit l'aire") {
    // Carre 10x10 mm, retrait 1 mm -> carre 8x8 mm = 64 mm².
    const auto result = inset_path_set(square(10'000), Micrometers{1'000});
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    const double area = std::abs(signed_area_um2((*result)[0].outer)) / 1e6;
    CHECK(area > 60.0);
    CHECK(area < 68.0);
}

TEST_CASE("inset : retrait nul rend la forme inchangee") {
    const auto result = inset_path_set(square(10'000), Micrometers{0});
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(std::abs(signed_area_um2((*result)[0].outer)) == 100'000'000.0);
}

TEST_CASE("inset : retrait trop grand fait disparaitre la forme") {
    const auto result = inset_path_set(square(10'000), Micrometers{6'000});
    REQUIRE(result.has_value());
    CHECK(result->empty());
}
