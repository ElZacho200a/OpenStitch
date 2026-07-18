// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/document/image_placement.hpp"

using namespace openstitch;
using namespace openstitch::document;

TEST_CASE("placement par largeur : ratio conserve") {
    // 200x100 px imposes a 50 mm de large -> 25 mm de haut, 0,25 mm/px
    const auto p = placement_from_width(200, 100, Millimeters{50.0});
    REQUIRE(p.has_value());
    CHECK(p->width.value == 50'000);
    CHECK(p->height.value == 25'000);
    CHECK(mm_per_pixel(*p, 200).value == 0.25);
    CHECK(p->center == Vec2um{});
}

TEST_CASE("placement par taille : ratio libre") {
    const auto p = placement_from_size(200, 100, Millimeters{30.0}, Millimeters{60.0});
    REQUIRE(p.has_value());
    CHECK(p->width.value == 30'000);
    CHECK(p->height.value == 60'000);
}

TEST_CASE("placement par dpi") {
    // 96 px a 96 dpi = 1 pouce = 25,4 mm
    const auto p = placement_from_dpi(96, 96, 96.0);
    REQUIRE(p.has_value());
    CHECK(p->width.value == 25'400);
    CHECK(p->height.value == 25'400);
}

TEST_CASE("entrees invalides refusees proprement") {
    CHECK_FALSE(placement_from_width(0, 100, Millimeters{50.0}).has_value());
    CHECK_FALSE(placement_from_width(200, 100, Millimeters{0.0}).has_value());
    CHECK_FALSE(placement_from_width(200, 100, Millimeters{-3.0}).has_value());
    CHECK_FALSE(placement_from_size(200, 100, Millimeters{10.0}, Millimeters{0.0}).has_value());
    CHECK_FALSE(placement_from_dpi(200, 100, 0.0).has_value());

    const auto err = placement_from_width(200, 100, Millimeters{-1.0});
    REQUIRE_FALSE(err.has_value());
    CHECK(err.error().category == ErrorCategory::UserInput);
}
