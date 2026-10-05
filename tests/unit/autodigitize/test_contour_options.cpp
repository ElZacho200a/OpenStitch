// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/auto_satin/graph_cleanup.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/autodigitize/contour_options.hpp"

using namespace openstitch;
using namespace openstitch::autodigitize;

TEST_CASE("contour thresholds anchor at detail 0.5 on existing defaults") {
    const auto t = contour_thresholds(0.5);
    CHECK(t.min_branch_length.value ==
          auto_satin::GraphCleanupParameters{}.minimum_branch_length.value);
    CHECK(t.simplify_tolerance.value == AutoOptions{}.simplify_tolerance.value);
    CHECK(t.min_isolated_length.value == 2 * 1200);
}

TEST_CASE("contour thresholds are monotonic in detail") {
    ContourThresholds prev = contour_thresholds(0.0);
    for (int i = 1; i <= 100; ++i) {
        const auto t = contour_thresholds(i / 100.0);
        CHECK(t.min_branch_length.value <= prev.min_branch_length.value);
        CHECK(t.min_loop_perimeter.value <= prev.min_loop_perimeter.value);
        CHECK(t.min_isolated_length.value <= prev.min_isolated_length.value);
        CHECK(t.simplify_tolerance.value <= prev.simplify_tolerance.value);
        CHECK(t.merge_distance.value <= prev.merge_distance.value);
        prev = t;
    }
    CHECK(contour_thresholds(0.0).min_branch_length.value >
          contour_thresholds(0.5).min_branch_length.value);
    CHECK(contour_thresholds(0.5).min_branch_length.value >
          contour_thresholds(1.0).min_branch_length.value - 1);
}

TEST_CASE("contour thresholds never go below the physical guards at detail 1") {
    const auto lim = contour_limits();
    const auto t = contour_thresholds(1.0);
    CHECK(t.min_branch_length.value >= lim.min_element_length.value);
    CHECK(t.min_loop_perimeter.value >= lim.min_loop_perimeter.value);
    CHECK(t.min_isolated_length.value >= lim.min_element_length.value);
    CHECK(t.simplify_tolerance.value >= lim.min_simplify_tolerance.value);
    // Out of range / NaN are clamped, never extrapolated.
    CHECK(contour_thresholds(5.0).min_branch_length.value == t.min_branch_length.value);
    CHECK(contour_thresholds(-3.0).min_branch_length.value ==
          contour_thresholds(0.0).min_branch_length.value);
}
