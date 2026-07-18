// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/core/units.hpp"

using namespace openstitch;
using namespace openstitch::literals;

TEST_CASE("conversion mm <-> um exacte") {
    CHECK(to_micrometers(Millimeters{1.0}).value == 1000);
    CHECK(to_micrometers(Millimeters{-2.5}).value == -2500);
    CHECK(to_millimeters(Micrometers{1500}).value == 1.5);
    CHECK(to_millimeters(to_micrometers(Millimeters{42.0})).value == 42.0);
}

TEST_CASE("arrondi au micrometre le plus proche") {
    CHECK(to_micrometers(Millimeters{0.0004}).value == 0);
    CHECK(to_micrometers(Millimeters{0.0006}).value == 1);
    CHECK(to_micrometers(Millimeters{-0.0006}).value == -1);
}

TEST_CASE("pixels vers micrometres via resolution explicite") {
    // 100 px à 0,5 mm/px = 50 mm = 50 000 um
    CHECK(to_micrometers(Pixels{100.0}, Millimeters{0.5}).value == 50000);
}

TEST_CASE("arithmetique des unites et vecteurs") {
    CHECK(Micrometers{300} + Micrometers{-100} == Micrometers{200});
    CHECK(-Micrometers{5} == Micrometers{-5});

    const Vec2um a{10_um, 20_um};
    const Vec2um b{1_um, 2_um};
    CHECK(a + b == Vec2um{11_um, 22_um});
    CHECK(a - b == Vec2um{9_um, 18_um});
    CHECK(length_um(Vec2um{3000_um, 4000_um}) == 5000.0);
}

TEST_CASE("le pas DST (100 um) se convertit exactement") {
    const Micrometers dst_step = to_micrometers(Millimeters{0.1});
    CHECK(dst_step.value == 100);
    CHECK(Micrometers{12100}.value % dst_step.value == 0);  // déplacement max DST : 12,1 mm
}
