// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>

#include "scrub.hpp"

using namespace openstitch::desktop_v2;
using Catch::Matchers::WithinAbs;

TEST_CASE("scrub moves one step per fixed number of pixels", "[scrub]") {
    CHECK_THAT(scrubbedValue(1.0, 0, 0.1, 0, 5), WithinAbs(1.0, 1e-12));
    CHECK_THAT(scrubbedValue(1.0, kScrubPixelsPerStep - 1, 0.1, 0, 5), WithinAbs(1.0, 1e-12));
    CHECK_THAT(scrubbedValue(1.0, kScrubPixelsPerStep, 0.1, 0, 5), WithinAbs(1.1, 1e-12));
    CHECK_THAT(scrubbedValue(1.0, -3 * kScrubPixelsPerStep, 0.1, 0, 5), WithinAbs(0.7, 1e-12));
}

TEST_CASE("scrub speed modifiers and clamping", "[scrub]") {
    const double dx = 2 * kScrubPixelsPerStep;
    CHECK_THAT(scrubbedValue(1.0, dx, 0.1, 0, 5, ScrubSpeed::Coarse), WithinAbs(3.0, 1e-12));
    CHECK_THAT(scrubbedValue(1.0, dx, 0.1, 0, 5, ScrubSpeed::Fine), WithinAbs(1.02, 1e-12));
    CHECK_THAT(scrubbedValue(4.9, 100 * dx, 0.1, 0, 5), WithinAbs(5.0, 1e-12));
    CHECK_THAT(scrubbedValue(0.1, -100 * dx, 0.1, 0, 5), WithinAbs(0.0, 1e-12));
}

TEST_CASE("scrub rejects invalid inputs without moving", "[scrub]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK_THAT(scrubbedValue(1.0, 50, 0.0, 0, 5), WithinAbs(1.0, 1e-12));  // pas nul
    CHECK_THAT(scrubbedValue(1.0, nan, 0.1, 0, 5), WithinAbs(1.0, 1e-12)); // déplacement non fini
    CHECK_THAT(scrubbedValue(9.0, 0, 0.1, 0, 5), WithinAbs(5.0, 1e-12));   // départ hors bornes
    CHECK_THAT(scrubbedValue(1.0, 50, 0.1, 5, 0), WithinAbs(5.0, 1e-12));  // intervalle inversé
}
