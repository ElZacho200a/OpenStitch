// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_render/thread_scene.hpp"

using openstitch::stitch_render::thread_scene_json;
using openstitch::stitch_render::ThreadSegment;

TEST_CASE("thread scene json lists segments in sewing order", "[thread_scene]") {
    const std::vector<ThreadSegment> segments{{0.0F, 0.0F, 1.5F, -2.25F, {10, 20, 30}},
                                              {1.5F, -2.25F, 3.0F, 0.0F, {200, 100, 0}}};
    const std::string json = thread_scene_json(segments, 0.35);
    CHECK(json.starts_with("{\"format\": \"openstitch-thread-scene\", \"version\": 1"));
    CHECK(json.find("\"thread_width_mm\": 0.35") != std::string::npos);
    const auto first = json.find("[0.0000, 0.0000, 1.5000, -2.2500, 10, 20, 30]");
    const auto second = json.find("[1.5000, -2.2500, 3.0000, 0.0000, 200, 100, 0]");
    REQUIRE(first != std::string::npos);
    REQUIRE(second != std::string::npos);
    CHECK(first < second);
    CHECK(json.ends_with("\n]}\n"));
    CHECK(thread_scene_json(segments, 0.35) == json); // déterministe
}

TEST_CASE("thread scene json of no segment is an empty list", "[thread_scene]") {
    CHECK(thread_scene_json({}, 0.4).find("\"segments\": [\n]}") != std::string::npos);
}
