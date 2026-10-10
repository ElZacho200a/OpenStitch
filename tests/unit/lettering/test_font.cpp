// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>

#include "test_support.hpp"

using namespace lettering_test;
using Catch::Approx;

TEST_CASE("font loads an embedded TrueType file") {
    const auto& f = vera();
    CHECK(f.family_name().find("Vera") != std::string::npos);
    CHECK(f.units_per_em() == Approx(2048.0));
    // Hauteur de capitale de Vera : 1493 unités.
    CHECK(f.cap_height_units() == Approx(1493.0).margin(2.0));
    CHECK(f.has_glyph(U'A'));
    CHECK(f.has_glyph(U'é')); // é
    CHECK_FALSE(f.has_glyph(U'中')); // idéogramme absent
}

TEST_CASE("font rejects garbage and empty data") {
    CHECK_FALSE(openstitch::lettering::Font::from_memory({}).has_value());
    CHECK_FALSE(openstitch::lettering::Font::from_memory({1, 2, 3, 4, 5, 6, 7, 8}).has_value());
    CHECK_FALSE(openstitch::lettering::Font::from_file("does/not/exist.ttf").has_value());
}

TEST_CASE("inspect_font_file reads family and style, and refuses non fonts") {
    const std::string dir = OPENSTITCH_FONT_DIR;
    const auto faces = openstitch::lettering::inspect_font_file(dir + "/VeraBd.ttf");
    REQUIRE(faces.size() == 1);
    CHECK(faces[0].family.find("Vera") != std::string::npos);
    CHECK(faces[0].style == "Bold");
    CHECK(openstitch::lettering::inspect_font_file(dir + "/nope.ttf").empty());
    CHECK(openstitch::lettering::inspect_font_file(dir + "/bitstream-vera-license.txt").empty());
}

TEST_CASE("glyph outlines: counters, advance and missing glyphs") {
    const auto& f = vera();
    const auto a = f.outline(U'A', 2.0);
    REQUIRE(a.present);
    CHECK(a.loops.size() == 2); // contour + contre-forme triangulaire
    CHECK(a.advance > 0.0);
    const auto o = f.outline(U'O', 2.0);
    CHECK(o.loops.size() == 2);
    const auto i = f.outline(U'i', 2.0);
    CHECK(i.loops.size() == 2); // fût + point
    const auto space = f.outline(U' ', 2.0);
    CHECK(space.present);
    CHECK(space.loops.empty());
    CHECK(space.advance > 0.0);
    const auto missing = f.outline(U'中', 2.0);
    CHECK_FALSE(missing.present);
    // Un aplatissement plus fin ne peut que densifier le contour.
    CHECK(f.outline(U'O', 0.5).loops[0].size() >= f.outline(U'O', 8.0).loops[0].size());
}

TEST_CASE("font kerning is deterministic and negative for AV") {
    const auto& f = vera();
    CHECK(f.kerning(U'A', U'V') == f.kerning(U'A', U'V'));
    CHECK(f.kerning(U'A', U'V') <= 0.0);
    CHECK(f.kerning(U'H', U'H') == 0.0);
    CHECK(f.kerning(U'A', U'中') == 0.0);
}
