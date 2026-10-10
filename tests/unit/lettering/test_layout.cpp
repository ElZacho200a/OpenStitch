// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numbers>

#include "test_support.hpp"

using namespace lettering_test;
using Catch::Approx;

namespace {

struct Box {
    double x0{1e18}, y0{1e18}, x1{-1e18}, y1{-1e18};
};

Box bounds(const lettering::PlacedGlyph& g) {
    Box b;
    for (const auto& set : g.shapes) {
        for (const auto& n : set.outer.nodes) {
            b.x0 = std::min(b.x0, static_cast<double>(n.pos.x.value));
            b.x1 = std::max(b.x1, static_cast<double>(n.pos.x.value));
            b.y0 = std::min(b.y0, static_cast<double>(n.pos.y.value));
            b.y1 = std::max(b.y1, static_cast<double>(n.pos.y.value));
        }
    }
    return b;
}

Box bounds(const lettering::TextLayout& l) {
    Box b;
    for (const auto& g : l.glyphs) {
        const Box gb = bounds(g);
        b.x0 = std::min(b.x0, gb.x0);
        b.x1 = std::max(b.x1, gb.x1);
        b.y0 = std::min(b.y0, gb.y0);
        b.y1 = std::max(b.y1, gb.y1);
    }
    return b;
}

lettering::TextLayout layout(const document::TextObject& t) {
    auto r = lettering::layout_text(vera(), t);
    REQUIRE(r.has_value());
    return std::move(*r);
}

} // namespace

TEST_CASE("utf8 decoding handles multibyte and invalid sequences") {
    CHECK(lettering::decode_utf8("A\xC3\xA9\xE2\x82\xAC") ==
          std::vector<char32_t>{U'A', U'é', U'€'});
    const auto bad = lettering::decode_utf8("a\xFF"
                                            "b\xC3");
    REQUIRE(bad.size() == 4);
    CHECK(bad[1] == 0xFFFD);
    CHECK(bad[3] == 0xFFFD);
    CHECK(lettering::encode_utf8(U'é') == "\xC3\xA9");
    CHECK(lettering::decode_utf8(lettering::encode_utf8(U'\U0001F600'))[0] == U'\U0001F600');
}

TEST_CASE("a capital H is exactly cap_height tall and sits on the baseline") {
    const auto l = layout(make_text("H", 10'000));
    REQUIRE(l.glyphs.size() == 1);
    const Box b = bounds(l.glyphs[0]);
    CHECK(b.y0 == Approx(0.0).margin(30.0));
    CHECK(b.y1 == Approx(10'000.0).margin(30.0));
    CHECK(l.line_count == 1);
}

TEST_CASE("letters follow the reading order and spaces produce no glyph") {
    const auto l = layout(make_text("A B"));
    REQUIRE(l.glyphs.size() == 2);
    CHECK(l.glyphs[0].code_point == U'A');
    CHECK(l.glyphs[1].code_point == U'B');
    CHECK(bounds(l.glyphs[1]).x0 > bounds(l.glyphs[0]).x1);
}

TEST_CASE("letter spacing and word spacing widen the text") {
    auto t = make_text("AB CD");
    const double base = bounds(layout(t)).x1 - bounds(layout(t)).x0;
    t.letter_spacing = Micrometers{1'000};
    const double spaced = bounds(layout(t)).x1 - bounds(layout(t)).x0;
    CHECK(spaced == Approx(base + 4 * 1'000.0).margin(40.0)); // 5 caractères : 4 intervalles
    t.letter_spacing = Micrometers{0};
    t.word_spacing = Micrometers{2'500};
    const double words = bounds(layout(t)).x1 - bounds(layout(t)).x0;
    CHECK(words == Approx(base + 2'500.0).margin(40.0));
}

TEST_CASE("kerning shrinks AV and can be disabled") {
    auto t = make_text("AV");
    t.kerning = true;
    const double kerned = bounds(layout(t)).x1;
    t.kerning = false;
    const double plain = bounds(layout(t)).x1;
    CHECK(kerned <= plain);
}

TEST_CASE("alignment: center and right place the line around the origin") {
    auto t = make_text("HELLO");
    t.origin = {Micrometers{50'000}, Micrometers{0}};
    t.align = document::TextAlign::Left;
    const Box left = bounds(layout(t));
    CHECK(left.x0 >= 50'000.0 - 1'000.0);
    t.align = document::TextAlign::Center;
    const Box center = bounds(layout(t));
    CHECK(((center.x0 + center.x1) / 2.0) == Approx(50'000.0).margin(1'500.0));
    t.align = document::TextAlign::Right;
    const Box right = bounds(layout(t));
    CHECK(right.x1 == Approx(50'000.0).margin(1'500.0));
}

TEST_CASE("multiline text stacks lines by cap height times line spacing") {
    auto t = make_text("H\nH", 10'000);
    t.line_spacing = 2.0;
    const auto l = layout(t);
    REQUIRE(l.glyphs.size() == 2);
    CHECK(l.line_count == 2);
    CHECK(l.glyphs[1].line == 1);
    const double dy = bounds(l.glyphs[0]).y0 - bounds(l.glyphs[1]).y0;
    CHECK(dy == Approx(20'000.0).margin(30.0));
}

TEST_CASE("justify stretches inner spaces up to the target width") {
    auto t = make_text("AA AA\nAA");
    t.align = document::TextAlign::Justify;
    t.justify_width = Micrometers{80'000};
    const auto l = layout(t);
    // Première ligne : bord droit du dernier A à ~80 mm ; dernière ligne : non étirée.
    double first_right = 0.0;
    double last_right = 0.0;
    for (const auto& g : l.glyphs) {
        (g.line == 0 ? first_right : last_right) =
            std::max(g.line == 0 ? first_right : last_right, bounds(g).x1);
    }
    CHECK(first_right == Approx(80'000.0).margin(1'500.0));
    CHECK(last_right < 40'000.0);
}

TEST_CASE("rotation by 90 degrees turns the baseline vertical") {
    auto t = make_text("HH");
    t.rotation = Angle{std::numbers::pi / 2.0};
    const Box b = bounds(layout(t));
    CHECK((b.y1 - b.y0) > 1.5 * (b.x1 - b.x0));
}

TEST_CASE("missing glyphs are omitted with a warning, never silently squared") {
    const auto l = layout(make_text("A\xE4\xB8\xAD"
                                    "B"));
    CHECK(l.glyphs.size() == 2);
    REQUIRE(l.warnings.size() == 1);
    CHECK(l.warnings[0].code == "glyphe-absent");
}

TEST_CASE("layout is deterministic") {
    auto t = make_text("Déterminisme\nTest", 9'000);
    t.align = document::TextAlign::Center;
    const auto a = layout(t);
    const auto b = layout(t);
    REQUIRE(a.glyphs.size() == b.glyphs.size());
    for (std::size_t i = 0; i < a.glyphs.size(); ++i) {
        CHECK(a.glyphs[i].shapes == b.glyphs[i].shapes);
        CHECK(a.glyphs[i].pen == b.glyphs[i].pen);
    }
}

TEST_CASE("empty text and non positive height") {
    CHECK(layout(make_text("")).glyphs.empty());
    auto t = make_text("A");
    t.cap_height = Micrometers{0};
    CHECK_FALSE(lettering::layout_text(vera(), t).has_value());
}
