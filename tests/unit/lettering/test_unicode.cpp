// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "test_support.hpp"

using namespace lettering_test;
using lettering::decode_utf8;

namespace {

using Cps = std::vector<char32_t>;

lettering::TextLayout layout(const std::string& utf8,
                             const lettering::LayoutOptions& options = {}) {
    auto r = lettering::layout_text(vera(), make_text(utf8), options);
    REQUIRE(r.has_value());
    return std::move(*r);
}

bool has_warning(const lettering::TextLayout& l, const std::string& code) {
    return std::any_of(l.warnings.begin(), l.warnings.end(),
                       [&](const auto& w) { return w.code == code; });
}

} // namespace

TEST_CASE("utf8 decoding rejects overlong, surrogate and truncated sequences") {
    // Surcodage C0 80, substitut ED A0 80, séquence 4 octets tronquée F0 9F 98.
    CHECK(decode_utf8("\xC0\x80") == Cps{0xFFFD, 0xFFFD});
    CHECK(decode_utf8("\xED\xA0\x80") == Cps{0xFFFD, 0xFFFD, 0xFFFD});
    CHECK(decode_utf8("a\xF0\x9F\x98") == Cps{U'a', 0xFFFD, 0xFFFD, 0xFFFD});
    CHECK(decode_utf8("\xF4\x90\x80\x80") == Cps{0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD}); // > U+10FFFF
    CHECK(decode_utf8("\x80") == Cps{0xFFFD});
    CHECK(decode_utf8("").empty());
}

TEST_CASE("utf8 round trip covers every plane and the boundaries") {
    for (char32_t cp : {char32_t{0x7F}, char32_t{0x80}, char32_t{0x7FF}, char32_t{0x800},
                        char32_t{0xFFFF}, char32_t{0x10000}, char32_t{0x10FFFF}}) {
        CHECK(decode_utf8(lettering::encode_utf8(cp)) == Cps{cp});
    }
}

TEST_CASE("unicode classification helpers") {
    CHECK(lettering::is_combining_mark(0x0301));
    CHECK_FALSE(lettering::is_combining_mark(U'e'));
    CHECK(lettering::is_ignorable(0x200B));
    CHECK(lettering::is_ignorable(0xFEFF));
    CHECK(lettering::is_ignorable(0x01));
    CHECK_FALSE(lettering::is_ignorable(U'\n'));
    CHECK_FALSE(lettering::is_ignorable(U'a'));
    CHECK(lettering::is_rtl_char(0x05D0));       // alef
    CHECK(lettering::is_rtl_char(0x0627));       // alif
    CHECK_FALSE(lettering::is_rtl_char(0x0661)); // chiffre arabo-indien : nombre, pas lettre
    CHECK(lettering::is_arabic_script(0x0627));
    CHECK_FALSE(lettering::is_arabic_script(0x05D0));
}

TEST_CASE("composition and decomposition tables agree") {
    CHECK(lettering::compose_pair(U'e', 0x0301) == U'é');
    CHECK(lettering::compose_pair(U'A', 0x030A) == U'Å');
    CHECK(lettering::compose_pair(U'n', 0x0303) == U'ñ');
    CHECK(lettering::compose_pair(U'c', 0x0327) == U'ç');
    CHECK(lettering::compose_pair(U'z', 0x030C) == U'ž');
    CHECK(lettering::compose_pair(0x0438, 0x0306) == 0x0439); // и + brève = й
    CHECK(lettering::compose_pair(0x03B1, 0x0301) == 0x03AC); // α + tonos = ά
    CHECK(lettering::compose_pair(U'x', 0x0301) == 0);
    char32_t base = 0;
    char32_t mark = 0;
    REQUIRE(lettering::decompose_char(U'ü', base, mark));
    CHECK(base == U'u');
    CHECK(mark == 0x0308);
    CHECK_FALSE(lettering::decompose_char(U'q', base, mark));
    // Aller-retour sur toute la plage latine 1 : composé -> (base, marque) -> composé.
    for (char32_t c = 0xC0; c <= 0x17F; ++c) {
        if (lettering::decompose_char(c, base, mark)) {
            CHECK(lettering::compose_pair(base, mark) == c);
        }
    }
}

TEST_CASE("fallback sequences for typographic characters") {
    CHECK(lettering::fallback_sequence(0x2019) == U"'");
    CHECK(lettering::fallback_sequence(0x201C) == U"\"");
    CHECK(lettering::fallback_sequence(0x2013) == U"-");
    CHECK(lettering::fallback_sequence(0x2026) == U"...");
    CHECK(lettering::fallback_sequence(0x3000) == U" ");
    CHECK(lettering::fallback_sequence(U'a').empty());
}

TEST_CASE("visual order leaves left to right text untouched") {
    const Cps ltr = decode_utf8("Hello, monde 42!");
    CHECK(lettering::visual_order(ltr) == ltr);
    CHECK(lettering::visual_order({}).empty());
}

TEST_CASE("visual order reverses a Hebrew run inside a left to right line") {
    // « ab אבג cd » : le passage hébreu s'affiche inversé, le reste est inchangé.
    const Cps logical = {U'a', U'b', U' ', 0x05D0, 0x05D1, 0x05D2, U' ', U'c', U'd'};
    const Cps visual = {U'a', U'b', U' ', 0x05D2, 0x05D1, 0x05D0, U' ', U'c', U'd'};
    CHECK(lettering::visual_order(logical) == visual);
}

TEST_CASE("visual order of a right to left paragraph reverses the whole line") {
    // Paragraphe RTL (premier fort = hébreu) avec un mot latin au milieu :
    // les mots sont dans l'ordre inverse, le mot latin garde son sens.
    const Cps logical = {0x05D0, 0x05D1, U' ', U'a', U'b', U' ', 0x05D2, 0x05D3};
    const Cps visual = {0x05D3, 0x05D2, U' ', U'a', U'b', U' ', 0x05D1, 0x05D0};
    CHECK(lettering::visual_order(logical) == visual);
}

TEST_CASE("visual order keeps numbers readable and mirrors brackets in RTL runs") {
    // Nombre dans un passage hébreu : les chiffres gardent leur ordre.
    const Cps numbers = {0x05D0, U' ', U'1', U'2', U'3', U' ', 0x05D1};
    const Cps numbers_visual = {0x05D1, U' ', U'1', U'2', U'3', U' ', 0x05D0};
    CHECK(lettering::visual_order(numbers) == numbers_visual);
    // Parenthèses entre deux lettres RTL : inversées ET miroirées.
    const Cps brackets = {0x05D0, U'(', 0x05D1, U')', 0x05D2};
    const Cps brackets_visual = {0x05D2, U'(', 0x05D1, U')', 0x05D0};
    CHECK(lettering::visual_order(brackets) == brackets_visual);
}

TEST_CASE("visual order keeps a combining mark attached to its letter") {
    const Cps logical = {0x05D0, 0x05B0, 0x05D1}; // alef + sheva, bet
    const Cps visual = lettering::visual_order(logical);
    REQUIRE(visual.size() == 3);
    CHECK(visual[0] == 0x05D1);
    CHECK(visual[1] == 0x05D0);
    CHECK(visual[2] == 0x05B0);
}

TEST_CASE("layout skips invisible characters without any warning") {
    const auto l = layout("A\xE2\x80\x8B"
                          "B\x01"
                          "\xEF\xBB\xBF");
    CHECK(l.glyphs.size() == 2);
    CHECK(l.warnings.empty());
}

TEST_CASE("decomposed accents render like the precomposed letter") {
    const auto precomposed = layout("\xC3\xA9"); // é
    const auto decomposed = layout("e\xCC\x81"); // e + U+0301
    REQUIRE(precomposed.glyphs.size() == 1);
    REQUIRE(decomposed.glyphs.size() == 1);
    CHECK(decomposed.glyphs[0].shapes == precomposed.glyphs[0].shapes);
    CHECK(decomposed.warnings.empty());

    lettering::LayoutOptions off;
    off.compose_marks = false;
    const auto raw = layout("e\xCC\x81", off);
    // Sans composition, la marque est traitée seule : lettre + marque (ou signalée).
    CHECK((raw.glyphs.size() == 2 || has_warning(raw, "glyphe-absent")));
}

TEST_CASE("a combining mark overlays the previous letter without advancing the pen") {
    // U+0323 (point souscrit) n'est dans aucune table : marque libre.
    const auto l = layout("e\xCC\xA3"
                          "x");
    if (vera().has_glyph(0x0323)) {
        REQUIRE(l.glyphs.size() == 3);
        CHECK(l.glyphs[1].pen == l.glyphs[0].pen);
        CHECK(l.glyphs[2].pen.x.value > l.glyphs[0].pen.x.value);
    } else {
        CHECK(has_warning(l, "glyphe-absent"));
        REQUIRE(l.glyphs.size() == 2);
        // Une marque absente n'ajoute aucune chasse : « e » et « x » restent contigus.
        const auto plain = layout("ex");
        CHECK(l.glyphs[1].pen == plain.glyphs[1].pen);
    }
}

TEST_CASE("absent typographic characters fall back with a warning") {
    // U+2011 (trait d'union insécable) : absent de Vera, repli sur « - ».
    if (vera().has_glyph(0x2011)) {
        SKIP("la police porte U+2011");
    }
    const auto l = layout("a\xE2\x80\x91"
                          "b");
    CHECK(l.glyphs.size() == 3);
    CHECK(has_warning(l, "glyphe-remplace"));
    CHECK_FALSE(has_warning(l, "glyphe-absent"));

    lettering::LayoutOptions off;
    off.fallback_glyphs = false;
    const auto raw = layout("a\xE2\x80\x91"
                            "b",
                            off);
    CHECK(raw.glyphs.size() == 2);
    CHECK(has_warning(raw, "glyphe-absent"));
}

TEST_CASE("non latin scripts missing from the font are reported, never fatal") {
    const auto greek = layout("\xCE\xB1\xCE\xB2\xCE\xB3 ok"); // αβγ ok
    CHECK(has_warning(greek, "glyphe-absent"));
    CHECK(greek.glyphs.size() >= 2); // « ok » est cousu

    const auto cjk = layout("\xE4\xB8\xAD\xE6\x96\x87"); // 中文
    CHECK(cjk.glyphs.empty());
    REQUIRE(cjk.warnings.size() == 1); // un seul avertissement groupé
    CHECK(cjk.warnings[0].code == "glyphe-absent");

    const auto hebrew = layout("\xD7\x90\xD7\x91 ab"); // אב ab
    CHECK(has_warning(hebrew, "glyphe-absent"));
    CHECK_FALSE(has_warning(hebrew, "ecriture-non-supportee"));

    const auto arabic = layout("\xD8\xA7\xD8\xA8"); // اب
    CHECK(has_warning(arabic, "ecriture-non-supportee"));

    // Plan supplémentaire (emoji) et U+FFFD d'une séquence invalide.
    const auto emoji = layout("A\xF0\x9F\x98\x80\xFF"
                              "B");
    CHECK(emoji.glyphs.size() >= 2);
    CHECK(has_warning(emoji, "glyphe-absent"));
}

TEST_CASE("ligatures are opt in and only used when the font has the glyph") {
    const auto plain = layout("fi");
    CHECK(plain.glyphs.size() == 2);
    lettering::LayoutOptions on;
    on.ligatures = true;
    const auto lig = layout("fi", on);
    if (vera().has_glyph(0xFB01)) {
        REQUIRE(lig.glyphs.size() == 1);
        CHECK(lig.glyphs[0].code_point == 0xFB01);
    } else {
        CHECK(lig.glyphs.size() == 2);
    }
    // « ffi » : la plus longue ligature d'abord, repli sur ff + i / f + fi.
    const auto triple = layout("ffi", on);
    CHECK(triple.glyphs.size() <= 3);
    CHECK(triple.warnings.empty());
}

TEST_CASE("layout with unicode text is deterministic") {
    const std::string s = "Caf\xC3\xA9 e\xCC\x81t\xC3\xA9 \xE2\x80\x99 \xCE\xB1 \xD7\x90";
    const auto a = layout(s);
    const auto b = layout(s);
    REQUIRE(a.glyphs.size() == b.glyphs.size());
    for (std::size_t i = 0; i < a.glyphs.size(); ++i) {
        CHECK(a.glyphs[i].shapes == b.glyphs[i].shapes);
    }
    CHECK(a.warnings.size() == b.warnings.size());
}
