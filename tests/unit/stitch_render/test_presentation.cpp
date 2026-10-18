// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cstddef>

#include "openstitch/stitch_render/presentation.hpp"

using namespace openstitch;
using namespace openstitch::stitch_render;

namespace {

stitch::StitchCommand cmd(std::int32_t xUm, std::int32_t yUm, stitch::CommandType type) {
    return {Vec2um{Micrometers{xUm}, Micrometers{yUm}}, type, ObjectId{},
            stitch::StitchPass::TopStitch};
}

// Deux lignes cousues de couleurs differentes, separees par un changement de fil.
stitch::StitchSequence two_blocks() {
    stitch::StitchSequence s;
    using T = stitch::CommandType;
    s.commands = {cmd(0, 0, T::Jump),
                  cmd(0, 0, T::Stitch),
                  cmd(20'000, 0, T::Stitch),
                  cmd(20'000, 0, T::ColorChange),
                  cmd(0, 8'000, T::Jump),
                  cmd(0, 8'000, T::Stitch),
                  cmd(20'000, 8'000, T::Stitch),
                  cmd(20'000, 8'000, T::End)};
    return s;
}

PresentationOptions plain_options() {
    PresentationOptions o;
    o.px_per_mm = 10.0;
    o.margin_mm = 2.0;
    o.params.fabric_texture = FabricTexture::Plain;
    return o;
}

} // namespace

TEST_CASE("presentation : les blocs de couleur recoivent des sources distinctes") {
    const auto blocks = assign_color_blocks(two_blocks());
    REQUIRE(blocks.commands.size() == 8);
    CHECK(blocks.commands.front().source.value == 1);
    CHECK(blocks.commands.back().source.value == 2);
    CHECK(block_palette_color(0) != block_palette_color(1));
    CHECK(block_palette_color(0) == block_palette_color(12));
}

TEST_CASE("presentation : image aux dimensions du design plus la marge") {
    const RasterImage img = render_machine_sequence(two_blocks(), plain_options());
    // Design 20 x 8 mm + 2 mm de marge de chaque cote, a 10 px/mm : environ 240 x 120 px.
    CHECK(img.width >= 235);
    CHECK(img.width <= 245);
    CHECK(img.height >= 115);
    CHECK(img.height <= 125);
    CHECK(img.rgba.size() == static_cast<std::size_t>(img.width) * img.height * 4);
}

TEST_CASE("presentation : rendu deterministe") {
    const RasterImage a = render_machine_sequence(two_blocks(), plain_options());
    const RasterImage b = render_machine_sequence(two_blocks(), plain_options());
    CHECK(a.width == b.width);
    CHECK(a.rgba == b.rgba);
}

TEST_CASE("presentation : fond transparent et alpha droit pour le PNG") {
    PresentationOptions o = plain_options();
    o.params.fabric_opaque = false;
    const RasterImage img = render_machine_sequence(two_blocks(), o);
    REQUIRE(img.width > 0);
    CHECK(img.rgba[3] == 0); // coin haut gauche : fond transparent
    const auto straight = to_straight_rgba(img);
    REQUIRE(straight.size() == img.rgba.size());
    CHECK(straight[3] == 0);
    bool opaqueSeen = false;
    for (std::size_t i = 3; i < straight.size(); i += 4) {
        if (straight[i] == 255) {
            opaqueSeen = true;
            break;
        }
    }
    CHECK(opaqueSeen);
}

TEST_CASE("presentation : sequence sans couture -> image vide") {
    stitch::StitchSequence s;
    s.commands = {cmd(0, 0, stitch::CommandType::Jump), cmd(0, 0, stitch::CommandType::End)};
    const RasterImage img = render_machine_sequence(s, plain_options());
    CHECK(img.width == 0);
    CHECK(img.height == 0);
}

TEST_CASE("presentation : resolution abaissee quand l'image depasse le plafond") {
    PresentationOptions o = plain_options();
    o.px_per_mm = 40.0;
    o.max_pixels = 10'000;
    const RasterImage img = render_machine_sequence(two_blocks(), o);
    REQUIRE(img.width > 0);
    CHECK(static_cast<std::size_t>(img.width) * img.height <= 10'000);
}
