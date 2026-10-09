// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>

#include "openstitch/stitch_render/segments.hpp"

using namespace openstitch;
using namespace openstitch::stitch_render;

namespace {

stitch::StitchCommand cmd(int x_mm, int y_mm, stitch::CommandType type, std::uint64_t src) {
    stitch::StitchCommand c;
    c.pos = Vec2um{Micrometers{x_mm * 1000}, Micrometers{y_mm * 1000}};
    c.type = type;
    c.source = ObjectId{src};
    return c;
}

const ThreadColorFn kRed = [](ObjectId) { return std::array<std::uint8_t, 3>{200, 10, 20}; };

} // namespace

TEST_CASE("segments connect consecutive stitches of one object", "[stitch_render][segments]") {
    stitch::StitchSequence seq;
    seq.commands = {cmd(0, 0, stitch::CommandType::Stitch, 1),
                    cmd(2, 0, stitch::CommandType::Stitch, 1),
                    cmd(2, 3, stitch::CommandType::Stitch, 1)};
    const auto segs = build_thread_segments(seq, seq.commands.size(), kRed);
    REQUIRE(segs.size() == 2);
    CHECK(segs[0].ax == 0.0F);
    CHECK(segs[0].bx == 2.0F);
    // Repère écran : Y vers le bas -> le modèle (Y haut) est inversé.
    CHECK(segs[1].ay == 0.0F);
    CHECK(segs[1].by == -3.0F);
    CHECK(segs[1].rgb == std::array<std::uint8_t, 3>{200, 10, 20});
}

TEST_CASE("segments are never drawn across objects or breaks", "[stitch_render][segments]") {
    stitch::StitchSequence seq;
    seq.commands = {cmd(0, 0, stitch::CommandType::Stitch, 1),
                    cmd(1, 0, stitch::CommandType::Stitch, 2), // autre objet
                    cmd(2, 0, stitch::CommandType::Stitch, 2),
                    cmd(3, 0, stitch::CommandType::ColorChange, 2), // rupture
                    cmd(4, 0, stitch::CommandType::Stitch, 2),
                    cmd(5, 0, stitch::CommandType::Trim, 2), // rupture
                    cmd(6, 0, stitch::CommandType::Stitch, 2),
                    cmd(7, 0, stitch::CommandType::Stitch, 2)};
    const auto segs = build_thread_segments(seq, seq.commands.size(), kRed);
    REQUIRE(segs.size() == 2);
    CHECK(segs[0].ax == 1.0F);
    CHECK(segs[0].bx == 2.0F);
    CHECK(segs[1].ax == 6.0F);
    CHECK(segs[1].bx == 7.0F);
}

TEST_CASE("segments skip hidden objects and zero-length stitches", "[stitch_render][segments]") {
    stitch::StitchSequence seq;
    seq.commands = {cmd(0, 0, stitch::CommandType::Stitch, 1),
                    cmd(0, 0, stitch::CommandType::Stitch, 1), // longueur nulle
                    cmd(1, 0, stitch::CommandType::Stitch, 1),
                    cmd(5, 5, stitch::CommandType::Stitch, 2),
                    cmd(6, 5, stitch::CommandType::Stitch, 2)};
    const ThreadColorFn only_two = [](ObjectId id) -> std::optional<std::array<std::uint8_t, 3>> {
        if (id.value == 2) {
            return std::array<std::uint8_t, 3>{1, 2, 3};
        }
        return std::nullopt;
    };
    const auto segs = build_thread_segments(seq, seq.commands.size(), only_two);
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].ax == 5.0F);
}

TEST_CASE("segments honour last_index for simulation", "[stitch_render][segments]") {
    stitch::StitchSequence seq;
    for (int i = 0; i < 6; ++i) {
        seq.commands.push_back(cmd(i, 0, stitch::CommandType::Stitch, 1));
    }
    CHECK(build_thread_segments(seq, 0, kRed).empty());
    CHECK(build_thread_segments(seq, 3, kRed).size() == 3);
    CHECK(build_thread_segments(seq, 1000, kRed).size() == 5);
    CHECK(build_thread_segments(stitch::StitchSequence{}, 10, kRed).empty());
}

TEST_CASE("bounds and hash of segments", "[stitch_render][segments]") {
    CHECK(segments_bounds({}).empty());
    std::vector<ThreadSegment> a{{1.0F, 2.0F, 4.0F, -1.0F, {1, 1, 1}}};
    const RectMm b = segments_bounds(a);
    CHECK(b.x0 == 1.0);
    CHECK(b.x1 == 4.0);
    CHECK(b.y0 == -1.0);
    CHECK(b.y1 == 2.0);
    CHECK(b.contains(RectMm{2, 0, 3, 1}));

    const auto h = hash_segments(a);
    CHECK(h == hash_segments(a));
    a[0].rgb[2] = 9;
    CHECK(h != hash_segments(a));
}
