// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_analysis/thread_consumption.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

ThreadUsage usage_of(std::uint8_t r, double lengthMm, std::size_t blocks) {
    ThreadUsage u;
    u.identity.rgb = {r, 0, 0};
    u.length_mm = lengthMm;
    u.color_blocks = blocks;
    return u;
}

} // namespace

TEST_CASE("thread consumption applies path factor, block waste and bobbin ratio") {
    ThreadConsumptionOptions o;
    o.top_path_factor = 1.0;
    o.waste_per_block_mm = 0.0;
    o.bobbin_per_top_ratio = 0.5;
    o.bobbin_capacity_m = 10.0;
    const auto r = thread_consumption({usage_of(1, 40'000.0, 1)}, o);
    REQUIRE(r.per_thread.size() == 1);
    CHECK(r.per_thread[0].sewn_m == Catch::Approx(40.0));
    CHECK(r.per_thread[0].top_m == Catch::Approx(40.0));
    CHECK(r.per_thread[0].bobbin_m == Catch::Approx(20.0));
    CHECK(r.total_bobbin_m == Catch::Approx(20.0));
    CHECK(r.bobbins_needed == 2);
    CHECK(r.last_bobbin_fill == Catch::Approx(1.0)); // pile deux canettes pleines
}

TEST_CASE("thread consumption default coefficients and partial last bobbin") {
    const auto r = thread_consumption({usage_of(1, 100'000.0, 2), usage_of(2, 50'000.0, 1)});
    REQUIRE(r.per_thread.size() == 2);
    // 100 m x 1,15 + 2 x 50 mm = 115,1 m ; 50 m x 1,15 + 50 mm = 57,55 m
    CHECK(r.per_thread[0].top_m == Catch::Approx(115.1));
    CHECK(r.per_thread[1].top_m == Catch::Approx(57.55));
    CHECK(r.total_top_m == Catch::Approx(172.65));
    CHECK(r.total_bobbin_m == Catch::Approx(172.65 * 0.35));
    CHECK(r.bobbins_needed == 1); // 60,4 m sur 100 m
    CHECK(r.last_bobbin_fill == Catch::Approx(0.604275));
}

TEST_CASE("thread consumption without thread or without capacity needs no bobbin") {
    CHECK(thread_consumption(std::vector<ThreadUsage>{}).bobbins_needed == 0);
    ThreadConsumptionOptions o;
    o.bobbin_capacity_m = 0.0;
    const auto r = thread_consumption({usage_of(1, 10'000.0, 1)}, o);
    CHECK(r.bobbins_needed == 0);
    CHECK(r.last_bobbin_fill == 0.0);
    CHECK(r.total_bobbin_m > 0.0);
}

TEST_CASE("thread consumption from a project sequence is per colour in first-use order") {
    document::Project p;
    for (int i = 0; i < 3; ++i) {
        document::EmbroideryObject e;
        e.id = ObjectId{static_cast<std::uint64_t>(i + 1)};
        e.rgb = i == 1 ? std::array<std::uint8_t, 3>{0, 114, 206}
                       : std::array<std::uint8_t, 3>{200, 16, 46};
        p.embroidery_objects.push_back(e);
    }
    stitch::StitchSequence s;
    for (const auto& e : p.embroidery_objects) {
        if (!s.commands.empty()) {
            s.commands.push_back({um(0, 0), CommandType::ColorChange, e.id});
        }
        for (int k = 0; k < 3; ++k) {
            s.commands.push_back({um(k * 1000, 0), CommandType::Stitch, e.id});
        }
    }
    ThreadConsumptionOptions o;
    o.top_path_factor = 1.0;
    o.waste_per_block_mm = 0.0;
    const auto r = thread_consumption(p, s, o);
    REQUIRE(r.per_thread.size() == 2);
    CHECK(r.per_thread[0].identity.rgb[0] == 200); // rouge : objets 1 et 3, 2 mm chacun
    CHECK(r.per_thread[0].sewn_m == Catch::Approx(0.004));
    CHECK(r.per_thread[1].sewn_m == Catch::Approx(0.002));
    const auto again = thread_consumption(p, s, o);
    CHECK(again.total_top_m == r.total_top_m);
}
