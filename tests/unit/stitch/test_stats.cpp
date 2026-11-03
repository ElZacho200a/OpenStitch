// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch/sequence.hpp"

using namespace openstitch;
using namespace openstitch::stitch;

namespace {
Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}
} // namespace

TEST_CASE("stats : compteurs, longueur de fil et bornes") {
    StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Jump, ObjectId{1}},
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(3'000, 0), CommandType::Stitch, ObjectId{1}},
        {um(3'000, 4'000), CommandType::Stitch, ObjectId{1}},
        {um(3'000, 4'000), CommandType::ColorChange, ObjectId{2}},
        {um(10'000, 4'000), CommandType::Jump, ObjectId{2}},
        {um(10'000, 4'000), CommandType::Stitch, ObjectId{2}},
        {um(10'000, 10'000), CommandType::Stitch, ObjectId{2}},
        {um(10'000, 10'000), CommandType::Trim, ObjectId{2}},
        {um(10'000, 10'000), CommandType::End, ObjectId{}},
    };
    const StitchStats stats = compute_stats(seq);
    CHECK(stats.stitches == 5);
    CHECK(stats.jumps == 2);
    CHECK(stats.trims == 1);
    CHECK(stats.color_changes == 1);
    // Fil : 3 + 4 mm (objet 1) + 6 mm (objet 2) ; le saut n'est pas du fil cousu.
    CHECK(stats.thread_length_um == 13'000.0);
    CHECK(stats.bounds.min == um(0, 0));
    CHECK(stats.bounds.max == um(10'000, 10'000));
}

TEST_CASE("stats : sequence vide") {
    const StitchStats stats = compute_stats(StitchSequence{});
    CHECK(stats.stitches == 0);
    CHECK(stats.thread_length_um == 0.0);
}

TEST_CASE("object_endpoints : premier et dernier point cousu par objet") {
    StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Jump, ObjectId{2}},
        {um(1'000, 0), CommandType::Stitch, ObjectId{2}},
        {um(2'000, 0), CommandType::Stitch, ObjectId{2}},
        {um(9'000, 0), CommandType::Trim, ObjectId{2}},
        {um(5'000, 5'000), CommandType::Stitch, ObjectId{1}},
        {um(6'000, 5'000), CommandType::Stitch, ObjectId{1}},
        {um(7'000, 0), CommandType::Stitch, ObjectId{0}},
        {um(3'000, 0), CommandType::Stitch, ObjectId{2}},
    };
    const auto ends = object_endpoints(seq);
    REQUIRE(ends.size() == 2);
    CHECK(ends[0].id == ObjectId{2});
    CHECK(ends[0].first == um(1'000, 0));
    CHECK(ends[0].last == um(3'000, 0));
    CHECK(ends[1].id == ObjectId{1});
    CHECK(ends[1].first == um(5'000, 5'000));
    CHECK(ends[1].last == um(6'000, 5'000));
    CHECK(object_endpoints(StitchSequence{}).empty());
}
