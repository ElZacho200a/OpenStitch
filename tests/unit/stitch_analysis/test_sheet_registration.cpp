// SPDX-License-Identifier: Apache-2.0
//
// Repérage entre couleurs : solveur de la feuille rejoué par bloc de couleur, décalage entre blocs,
// contre-compensation.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/stitch_analysis/sheet_registration.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;
using stitch::StitchPass;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

void add_stitch(stitch::StitchSequence& s, Vec2um a, Vec2um b, std::uint64_t object) {
    s.commands.push_back({a, CommandType::Stitch, ObjectId{object}, StitchPass::TopStitch});
    s.commands.push_back({b, CommandType::Stitch, ObjectId{object}, StitchPass::TopStitch});
}

void add_color_change(stitch::StitchSequence& s, Vec2um at) {
    s.commands.push_back({at, CommandType::ColorChange, ObjectId{}, StitchPass::TopStitch});
}

// Deux blocs de couleur cousant le même trait horizontal de 6 mm.
stitch::StitchSequence two_blocks() {
    stitch::StitchSequence s;
    add_stitch(s, um(-3000, 0), um(3000, 0), 1);
    add_color_change(s, um(3000, 0));
    add_stitch(s, um(-3000, 0), um(3000, 0), 2);
    s.commands.push_back({um(3000, 0), CommandType::End, ObjectId{}, StitchPass::TopStitch});
    return s;
}

const SheetDomain kDomain{um(-30000, -30000), um(30000, 30000)};
constexpr Micrometers kCell{1000};

SheetParams tight() {
    SheetParams p;
    p.tolerance = 1e-10;
    return p;
}

} // namespace

TEST_CASE("sheet registration: an empty sequence gives an empty result", "[sheet][registration]") {
    const auto r = measure_registration({}, tight(), {}, kDomain, kCell);
    CHECK(r.empty());
    CHECK(r.blocks.empty());
}

TEST_CASE("sheet registration: blocks are cut at colour changes", "[sheet][registration]") {
    const auto r = measure_registration(two_blocks(), tight(), {}, kDomain, kCell);
    REQUIRE_FALSE(r.empty());
    REQUIRE(r.blocks.size() == 2);
    CHECK(r.blocks[0].first_command == 0);
    CHECK(r.blocks[0].end_command == 3);
    CHECK(r.blocks[1].first_command == 3);
    // La commande End finale reste dans le dernier bloc.
    CHECK(r.blocks[1].end_command == 6);
    CHECK(r.blocks[0].stitch_count == 2);
    CHECK(r.blocks[0].first_object.value == 1);
    CHECK(r.blocks[1].first_object.value == 2);
    CHECK(r.command_offset_um.size() == 6);
}

TEST_CASE("sheet registration: earlier blocks are dragged more than the last one",
          "[sheet][registration]") {
    const auto r = measure_registration(two_blocks(), tight(), {}, kDomain, kCell);
    REQUIRE(r.blocks.size() == 2);
    CHECK(r.blocks[0].max_offset_mm > 0.0);
    CHECK(r.blocks[0].max_offset_mm > 1.5 * r.blocks[1].max_offset_mm);
    CHECK(r.max_gap_mm > 0.0);
    CHECK(r.max_offset_mm == Catch::Approx(r.blocks[0].max_offset_mm));
    // Le premier bloc cousait une pointe tirée vers l'intérieur : l'écart pointe vers l'intérieur
    // aussi, donc vers le centre du trait (extrémité gauche -> +x).
    CHECK(r.command_offset_um[0].x.value > 0);
    CHECK(r.command_offset_um[1].x.value < 0);
}

TEST_CASE("sheet registration: a single block has no gap between colours",
          "[sheet][registration]") {
    stitch::StitchSequence s;
    add_stitch(s, um(-3000, 0), um(3000, 0), 1);
    const auto r = measure_registration(s, tight(), {}, kDomain, kCell);
    REQUIRE_FALSE(r.empty());
    CHECK(r.blocks.size() == 1);
    CHECK(r.max_gap_mm == 0.0);
    CHECK(r.gap_area_mm2 == 0.0);
}

TEST_CASE("sheet registration: blocks beyond the cap are merged", "[sheet][registration]") {
    stitch::StitchSequence s;
    for (std::uint64_t k = 1; k <= 5; ++k) {
        const auto y = static_cast<std::int32_t>(k) * 500;
        add_stitch(s, um(-3000, y), um(3000, y), k);
        add_color_change(s, um(3000, y));
    }
    RegistrationParams reg;
    reg.max_blocks = 64;
    CHECK(measure_registration(s, tight(), reg, kDomain, kCell).blocks.size() == 5);
    reg.max_blocks = 2;
    const auto merged = measure_registration(s, tight(), reg, kDomain, kCell);
    CHECK(merged.blocks.size() == 2);
    CHECK(merged.blocks.back().end_command == s.commands.size());
}

TEST_CASE("sheet registration: gap map is transparent where only one block sews",
          "[sheet][registration]") {
    const auto r = measure_registration(two_blocks(), tight(), {}, kDomain, kCell);
    const SheetRaster raster = rasterize_registration_gap(r, 0.3);
    CHECK(raster.width == 60);
    CHECK(raster.height == 60);
    bool any = false;
    for (std::size_t i = 3; i < raster.rgba.size(); i += 4) {
        any = any || raster.rgba[i] != 0;
    }
    CHECK(any);
    CHECK(raster.rgba[3] == 0); // coin du cadre : aucun bloc
    CHECK(rasterize_registration_gap({}, 0.3).rgba.empty());
}

TEST_CASE("sheet registration: compensation cancels most of the predicted offset",
          "[sheet][registration]") {
    const auto seq = two_blocks();
    const auto comp = compensate_registration(seq, tight(), {}, kDomain, kCell, 1.0, 3);
    REQUIRE(comp.sequence.commands.size() == seq.commands.size());
    CHECK(comp.iterations == 3);
    CHECK(comp.max_offset_before_mm > 0.0);
    CHECK(comp.max_offset_after_mm < 0.2 * comp.max_offset_before_mm);
    CHECK(comp.max_gap_after_mm < 0.2 * comp.max_gap_before_mm);
    // Les types de commande et les objets sources ne changent pas, seules les positions bougent.
    for (std::size_t i = 0; i < seq.commands.size(); ++i) {
        CHECK(comp.sequence.commands[i].type == seq.commands[i].type);
        CHECK(comp.sequence.commands[i].source == seq.commands[i].source);
    }
    // Le premier point du premier bloc part vers l'extérieur (opposé de son écart).
    CHECK(comp.sequence.commands[0].pos.x.value < seq.commands[0].pos.x.value);
}

TEST_CASE("sheet registration: zero strength or zero iterations leave the sequence unchanged",
          "[sheet][registration]") {
    const auto seq = two_blocks();
    const auto none = compensate_registration(seq, tight(), {}, kDomain, kCell, 0.0, 2);
    CHECK(none.sequence.commands == seq.commands);
    const auto zero = compensate_registration(seq, tight(), {}, kDomain, kCell, 1.0, 0);
    CHECK(zero.sequence.commands == seq.commands);
    CHECK(zero.iterations == 0);
    const auto empty = compensate_registration({}, tight(), {}, kDomain, kCell);
    CHECK(empty.sequence.commands.empty());
}

TEST_CASE("sheet registration: same input gives the same output", "[sheet][registration]") {
    const auto seq = two_blocks();
    const auto a = compensate_registration(seq, SheetParams{}, {}, kDomain, kCell, 1.0, 2);
    const auto b = compensate_registration(seq, SheetParams{}, {}, kDomain, kCell, 1.0, 2);
    CHECK(a.sequence.commands == b.sequence.commands);
    CHECK(a.max_gap_after_mm == b.max_gap_after_mm);
}
