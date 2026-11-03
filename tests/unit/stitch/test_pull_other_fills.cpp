// SPDX-License-Identifier: Apache-2.0
//
// Compensation du tirage des remplissages radial, spirale, concentrique (echo), motif et croise
// (HP-STI-033) : les extremites du trace depassent du contour de `pull`.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "openstitch/stitch_generation/contour_fill.hpp"
#include "openstitch/stitch_generation/crosshatch_fill.hpp"
#include "openstitch/stitch_generation/motif_fill.hpp"
#include "openstitch/stitch_generation/pull_auto.hpp"
#include "openstitch/stitch_generation/radial_fill.hpp"
#include "openstitch/stitch_generation/spiral_fill.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

constexpr std::int32_t kSide = 20'000;
constexpr std::int32_t kPull = 300;

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

geometry::PathNode node(std::int32_t x, std::int32_t y) {
    return geometry::PathNode{um(x, y), geometry::NodeType::Corner, std::nullopt, std::nullopt};
}

geometry::PathSet square() {
    geometry::Path p;
    p.closed = true;
    p.nodes = {node(0, 0), node(kSide, 0), node(kSide, kSide), node(0, kSide)};
    return geometry::PathSet{p, {}};
}

struct Box {
    std::int32_t x0{1'000'000}, y0{1'000'000}, x1{-1'000'000}, y1{-1'000'000};
};

Box box_of(const std::vector<FillStitch>& fill) {
    Box b;
    for (const FillStitch& fs : fill) {
        b.x0 = std::min(b.x0, fs.pos.x.value);
        b.y0 = std::min(b.y0, fs.pos.y.value);
        b.x1 = std::max(b.x1, fs.pos.x.value);
        b.y1 = std::max(b.y1, fs.pos.y.value);
    }
    return b;
}

} // namespace

TEST_CASE("pull other fills: crosshatch rows overshoot by exactly pull", "[pull][HP-STI-033]") {
    document::CrosshatchFillParams params;
    params.inset = Micrometers{0};
    const Box base = box_of(fill_crosshatch(square(), params));
    params.pull_compensation = Micrometers{kPull};
    const Box pulled = box_of(fill_crosshatch(square(), params));
    // Couche 0 horizontale : bouts en x ; couche 1 verticale : bouts en y.
    CHECK(pulled.x1 - base.x1 == kPull);
    CHECK(base.x0 - pulled.x0 == kPull);
    CHECK(pulled.y1 - base.y1 == kPull);
    CHECK(base.y0 - pulled.y0 == kPull);
}

TEST_CASE("pull other fills: radial rays overshoot by exactly pull", "[pull][HP-STI-033]") {
    document::RadialFillParams params;
    params.inset = Micrometers{0};
    params.center = um(kSide / 2, kSide / 2);
    const Box base = box_of(fill_radial(square(), params));
    params.pull_compensation = Micrometers{kPull};
    const Box pulled = box_of(fill_radial(square(), params));
    // Les rayons a 0 et 180 degres sont horizontaux : leur bout depasse de `pull`.
    CHECK(std::abs((pulled.x1 - base.x1) - kPull) <= 1);
    CHECK(std::abs((base.x0 - pulled.x0) - kPull) <= 1);
    CHECK(std::abs((pulled.y1 - base.y1) - kPull) <= 1);
    CHECK(std::abs((base.y0 - pulled.y0) - kPull) <= 1);
}

TEST_CASE("pull other fills: echo outer ring sits pull outside the contour", "[pull][HP-STI-033]") {
    document::ContourFillParams params;
    params.inset = Micrometers{0};
    const Box base = box_of(fill_contour(square(), params));
    params.pull_compensation = Micrometers{kPull};
    const Box pulled = box_of(fill_contour(square(), params));
    CHECK(pulled.x1 - base.x1 == kPull);
    CHECK(base.x0 - pulled.x0 == kPull);
    CHECK(pulled.y1 - base.y1 == kPull);
    CHECK(base.y0 - pulled.y0 == kPull);
}

TEST_CASE("pull other fills: spiral and motif exceed the contour but never more than pull",
          "[pull][HP-STI-033]") {
    {
        document::SpiralFillParams params;
        params.inset = Micrometers{0};
        const Box base = box_of(fill_spiral(square(), params));
        params.pull_compensation = Micrometers{kPull};
        const Box pulled = box_of(fill_spiral(square(), params));
        CHECK(pulled.x1 <= kSide + kPull + 1);
        CHECK(pulled.y1 <= kSide + kPull + 1);
        CHECK(pulled.x0 >= -kPull - 1);
        CHECK(pulled.y0 >= -kPull - 1);
        CHECK(pulled.x1 >= base.x1);
        CHECK((pulled.x1 > kSide || pulled.y1 > kSide || pulled.x0 < 0 || pulled.y0 < 0));
    }
    {
        document::MotifFillParams params;
        params.inset = Micrometers{0};
        params.pull_compensation = Micrometers{kPull};
        const Box pulled = box_of(fill_motif(square(), params));
        CHECK(pulled.x1 <= kSide + kPull + 1);
        CHECK(pulled.y1 <= kSide + kPull + 1);
        CHECK(pulled.x0 >= -kPull - 1);
        CHECK(pulled.y0 >= -kPull - 1);
    }
}

TEST_CASE("pull other fills: default pull 0 leaves the output unchanged", "[pull][HP-STI-033]") {
    document::RadialFillParams radial;
    CHECK(radial.pull_compensation.value == 0);
    CHECK(radial.pull_mode == document::PullMode::Manual);
    const auto a = fill_radial(square(), radial);
    radial.pull_compensation = Micrometers{0};
    const auto b = fill_radial(square(), radial);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].pos == b[i].pos);
    }
    document::SpiralFillParams spiral;
    const auto s0 = fill_spiral(square(), spiral);
    CHECK(!s0.empty());
}

TEST_CASE("pull other fills: Auto mode resolves a pull in the fill range", "[pull][HP-STI-033]") {
    document::SpiralFillParams params;
    params.pull_mode = document::PullMode::Auto;
    const auto resolved = resolve_pull_compensation(params, square());
    CHECK(resolved.pull_compensation.value >= 150);
    CHECK(resolved.pull_compensation.value <= 400);

    document::RadialFillParams manual;
    manual.pull_compensation = Micrometers{250};
    CHECK(resolve_pull_compensation(manual, square()).pull_compensation.value == 250);
}
