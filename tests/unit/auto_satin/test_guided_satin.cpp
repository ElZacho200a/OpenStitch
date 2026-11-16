// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>

#include "openstitch/auto_satin/satin_strategy.hpp"
#include "openstitch/auto_satin/shapes.hpp"

namespace as = openstitch::auto_satin;

namespace {

openstitch::Vec2um pt(std::int32_t x, std::int32_t y) {
    return {{x}, {y}};
}

bool same(const as::SkeletonSatinResult& a, const as::SkeletonSatinResult& b) {
    if (a.columns.size() != b.columns.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.columns.size(); ++i) {
        const auto& aa = a.columns[i].crossings;
        const auto& bb = b.columns[i].crossings;
        if (aa.size() != bb.size()) {
            return false;
        }
        for (std::size_t k = 0; k < aa.size(); ++k) {
            if (!(aa[k].a == bb[k].a) || !(aa[k].b == bb[k].b)) {
                return false;
            }
        }
    }
    return true;
}
} // namespace

TEST_CASE("guided satin: available as a fourth independent strategy", "[guided_satin]") {
    const auto strategies = as::available_strategies();
    REQUIRE(strategies.size() == 4);
    CHECK(strategies.back() == as::SatinStrategy::Guided);
    CHECK(as::strategy_id(as::SatinStrategy::Guided) == "guided_satin");

    const auto region = as::make_shape("rectangle");
    REQUIRE(region.has_value());
    as::SkeletonSatinParameters params;
    params.strategy = as::SatinStrategy::Guided;
    const auto result = as::generate_satin(*region, params);
    REQUIRE(result.has_value());
    CHECK_FALSE(result->columns.empty());
    CHECK(result->axes.size() == result->columns.size());
    for (const auto& col : result->columns) {
        CHECK(col.crossings.size() >= 2);
    }
}

TEST_CASE("guided satin: user curve changes crossings and remains deterministic",
          "[guided_satin]") {
    const auto region = as::make_shape("rectangle");
    REQUIRE(region.has_value());
    std::int32_t xmin = INT32_MAX, xmax = INT32_MIN, ymin = INT32_MAX, ymax = INT32_MIN;
    for (const auto& node : region->outer.nodes) {
        xmin = std::min(xmin, node.pos.x.value);
        xmax = std::max(xmax, node.pos.x.value);
        ymin = std::min(ymin, node.pos.y.value);
        ymax = std::max(ymax, node.pos.y.value);
    }
    REQUIRE(xmin < xmax);
    REQUIRE(ymin < ymax);
    const std::int32_t cx = xmin + (xmax - xmin) / 2;
    const std::int32_t cy = ymin + (ymax - ymin) / 2;
    const std::int32_t dx = (xmax - xmin) / 5;
    const std::int32_t dy = (ymax - ymin) / 8;
    as::SkeletonSatinParameters params;
    params.strategy = as::SatinStrategy::Guided;
    as::SkeletonSatinGuide guide;
    guide.anchor = pt(cx - dx, cy);
    guide.absolute = true;
    guide.path = {pt(cx - dx, cy), pt(cx, cy + dy), pt(cx + dx, cy)};
    params.guides.push_back(guide);

    const auto first = as::generate_satin(*region, params);
    const auto second = as::generate_satin(*region, params);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(same(*first, *second));
    CHECK_FALSE(first->columns.empty());
    CHECK(first->diagnostics.orphan_guides == 0);

    const auto automatic = as::generate_satin(as::SatinStrategy::Guided, *region, {});
    REQUIRE(automatic.has_value());
    CHECK_FALSE(same(*first, *automatic));
}

TEST_CASE("guided satin: a curve outside region cannot bridge the empty area", "[guided_satin]") {
    const auto region = as::make_shape("rectangle");
    REQUIRE(region.has_value());
    as::SkeletonSatinParameters params;
    params.strategy = as::SatinStrategy::Guided;
    as::SkeletonSatinGuide guide;
    guide.anchor = pt(1'000'000, 1'000'000);
    guide.path = {pt(1'000'000, 1'000'000), pt(1'005'000, 1'000'000)};
    params.guides.push_back(guide);
    const auto result = as::generate_satin(*region, params);
    REQUIRE(result.has_value());
    CHECK(result->columns.empty());
    CHECK(result->diagnostics.outside_samples > 0);
}
