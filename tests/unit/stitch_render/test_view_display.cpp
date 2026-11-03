// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_render/view_display.hpp"

using namespace openstitch;
using namespace openstitch::stitch_render;

namespace {

stitch::StitchCommand cmd(int x_mm, int y_mm, stitch::CommandType type, std::uint64_t src,
                          stitch::StitchPass pass = stitch::StitchPass::TopStitch) {
    stitch::StitchCommand c;
    c.pos = Vec2um{Micrometers{x_mm * 1000}, Micrometers{y_mm * 1000}};
    c.type = type;
    c.source = ObjectId{src};
    c.pass = pass;
    return c;
}

} // namespace

TEST_CASE("pass visibility follows each flag", "[stitch_render][view]") {
    PassDisplay d;
    REQUIRE(d.all_passes_visible());
    d.underlay = false;
    CHECK_FALSE(pass_visible(d, stitch::StitchPass::Underlay));
    CHECK(pass_visible(d, stitch::StitchPass::TopStitch));
    d.lock = false;
    CHECK_FALSE(pass_visible(d, stitch::StitchPass::Lock));
    CHECK_FALSE(d.all_passes_visible());
    CHECK(pass_tint(stitch::StitchPass::Underlay) != pass_tint(stitch::StitchPass::TopStitch));
}

TEST_CASE("count_passes tallies stitches per pass", "[stitch_render][view]") {
    stitch::StitchSequence seq;
    seq.commands = {cmd(0, 0, stitch::CommandType::Stitch, 1, stitch::StitchPass::Lock),
                    cmd(1, 0, stitch::CommandType::Stitch, 1, stitch::StitchPass::Underlay),
                    cmd(2, 0, stitch::CommandType::Stitch, 1, stitch::StitchPass::Underlay),
                    cmd(3, 0, stitch::CommandType::Jump, 1),
                    cmd(3, 0, stitch::CommandType::Trim, 1)};
    const PassCounts c = count_passes(seq);
    CHECK(c.stitches[static_cast<std::size_t>(stitch::StitchPass::Lock)] == 1);
    CHECK(c.stitches[static_cast<std::size_t>(stitch::StitchPass::Underlay)] == 2);
    CHECK(c.jumps == 1);
    CHECK(c.trims == 1);
}

TEST_CASE("function markers: entry per chain and cut at trim", "[stitch_render][view]") {
    stitch::StitchSequence seq;
    seq.commands = {
        cmd(0, 0, stitch::CommandType::Stitch, 1), cmd(2, 0, stitch::CommandType::Stitch, 1),
        cmd(2, 0, stitch::CommandType::Trim, 1), cmd(9, 0, stitch::CommandType::Jump, 2),
        cmd(9, 0, stitch::CommandType::Stitch, 2)};
    const auto markers = collect_function_markers(seq);
    REQUIRE(markers.size() == 3);
    CHECK(markers[0].kind == FunctionKind::Entry);
    CHECK(markers[1].kind == FunctionKind::Cut);
    CHECK(markers[1].x == 2.0F);
    CHECK(markers[2].kind == FunctionKind::Entry);
    CHECK(markers[2].x == 9.0F);
    CHECK(collect_function_markers(stitch::StitchSequence{}).empty());
}

TEST_CASE("grid settings are clamped", "[stitch_render][view]") {
    GridSettings s;
    s.step_mm = 0.001;
    s.major_every = 0;
    const GridSettings c = sanitized(s);
    CHECK(c.step_mm == kGridStepMinMm);
    CHECK(c.major_every == 1);
    s.step_mm = 1e9;
    s.major_every = 999;
    CHECK(sanitized(s).step_mm == kGridStepMaxMm);
    CHECK(sanitized(s).major_every == 50);
    s.step_mm = -3.0;
    CHECK(sanitized(s).step_mm == 0.0);
}

TEST_CASE("grid plan is automatic, custom or hidden", "[stitch_render][view]") {
    GridSettings s;
    const GridPlan automatic = plan_grid(s, 10.0);
    CHECK(automatic.minor_mm > 0.0);
    CHECK(automatic.major_mm >= automatic.minor_mm);

    s.step_mm = 2.0;
    s.major_every = 5;
    const GridPlan custom = plan_grid(s, 10.0);
    CHECK(custom.minor_mm == 2.0);
    CHECK(custom.major_mm == 10.0);

    // Fort dézoom : le pas fin est regroupé pour rester lisible.
    const GridPlan far = plan_grid(s, 0.5);
    CHECK(far.minor_mm * 0.5 >= 6.0);
    CHECK(far.major_mm == far.minor_mm * 5.0);

    s.visible = false;
    const GridPlan hidden = plan_grid(s, 10.0);
    CHECK(hidden.minor_mm == 0.0);
    CHECK(hidden.major_mm == 0.0);

    s.visible = true;
    s.major_every = 1;
    const GridPlan single = plan_grid(s, 10.0);
    CHECK(single.minor_mm == 0.0);
    CHECK(single.major_mm == 2.0);
}

TEST_CASE("grid positions are multiples of the step and bounded", "[stitch_render][view]") {
    const auto xs = grid_positions(2.0, -3.0, 5.0);
    REQUIRE(xs.size() == 4); // multiples de 2 dans [-3 ; 5] : -2, 0, 2, 4
    CHECK(xs.front() == -2.0);
    CHECK(xs.back() == 4.0);
    CHECK(grid_positions(0.0, 0.0, 10.0).empty());
    CHECK(grid_positions(0.1, 0.0, 1e6).empty()); // trop de lignes : rien
    CHECK(grid_positions(1.0, 5.0, 4.0).empty());
}

TEST_CASE("guides round-trip and tolerate junk", "[stitch_render][view]") {
    std::vector<Guide> guides = {Guide{GuideAxis::Horizontal, Micrometers{-5000}},
                                 Guide{GuideAxis::Vertical, Micrometers{12000}},
                                 Guide{GuideAxis::Vertical, Micrometers{12000}}};
    const std::string text = serialize_guides(guides);
    CHECK(text == "v:12000;h:-5000");
    const auto back = parse_guides(text);
    REQUIRE(back.size() == 2);
    CHECK(back[0].axis == GuideAxis::Vertical);
    CHECK(back[1].position.value == -5000);

    const auto junk = parse_guides("x:1;v:abc;v:;h:7;;v:3000x;h:99999999999");
    REQUIRE(junk.size() == 1);
    CHECK(junk[0].position.value == 7);
    CHECK(parse_guides("").empty());
}
