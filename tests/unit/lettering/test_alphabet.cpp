// SPDX-License-Identifier: Apache-2.0
// Acceptation HP-TXT-004 : un alphabet complet (A-Z, a-z, 0-9, accents FR) dans une
// police sans empattement donne 100 % de lettres cousues, sans zone oubliée ni
// débordement.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <variant>

#include "openstitch/stitch_generation/overrides.hpp"
#include "test_support.hpp"

using namespace lettering_test;

namespace {

const std::u32string kAlphabet = U"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
                                 U"àâäçéèêëîïôöùûü"
                                 U"ÀÉÇ";

struct Totals {
    int satin{0};
    int tatami{0};
    int contour{0};
    std::string tatami_letters; // lettres cousues en repli tatami (diagnostic)
};

// Teste chaque glyphe d'une police : points générés, dans la boîte (+ marge), non vides.
Totals run_alphabet(const lettering::Font& font, std::int32_t cap_um, document::TextFill fill) {
    Totals totals;
    for (const char32_t cp : kAlphabet) {
        if (!font.has_glyph(cp)) {
            continue;
        }
        const std::string ch = lettering::encode_utf8(cp);
        INFO("glyph " << ch);
        auto t = make_text(ch, cap_um);
        t.fill = fill;
        IdGenerator<ObjectId> ids;
        ids.reset(1);
        auto built = lettering::build_text_objects(font, t, ids);
        REQUIRE(built.has_value());
        // Un glyphe = au plus un objet par type de point (corps satin + points tatami).
        REQUIRE(built->embroideries.size() >= 1);
        REQUIRE(built->embroideries.size() <= 3);
        bool any_satin = false;
        bool any_tatami = false;
        for (const auto& emb : built->embroideries) {
            any_satin = any_satin || emb.is_auto_satin();
            any_tatami = any_tatami || emb.is_tatami();
        }
        if (any_satin) {
            ++totals.satin;
        } else if (any_tatami) {
            ++totals.tatami;
            totals.tatami_letters += ch;
        } else {
            ++totals.contour;
        }

        // Boîte du glyphe (tous ses morceaux).
        std::int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
        for (const auto& vector : built->vectors) {
            for (const auto& set : vector.paths) {
                for (const auto& n : set.outer.nodes) {
                    x0 = std::min(x0, n.pos.x.value);
                    x1 = std::max(x1, n.pos.x.value);
                    y0 = std::min(y0, n.pos.y.value);
                    y1 = std::max(y1, n.pos.y.value);
                }
            }
        }
        document::Project project;
        project.text_objects.push_back(t);
        project.vector_objects = built->vectors;
        project.embroidery_objects = built->embroideries;
        const auto seq = stitch_generation::effective_sequence(project);
        REQUIRE(seq.has_value());
        std::size_t stitches = 0;
        for (const auto& c : seq->commands) {
            if (c.type != stitch::CommandType::Stitch) {
                continue;
            }
            ++stitches;
            // Débordement : jamais au-delà de 0,6 mm de la boîte du glyphe.
            CHECK(c.pos.x.value >= x0 - 600);
            CHECK(c.pos.x.value <= x1 + 600);
            CHECK(c.pos.y.value >= y0 - 600);
            CHECK(c.pos.y.value <= y1 + 600);
        }
        // Lettre cousue : jamais vide.
        CHECK(stitches >= 8);
    }
    return totals;
}

} // namespace

TEST_CASE("alphabet in a bold sans-serif is fully sewn at 15 mm") {
    const auto totals = run_alphabet(vera_bold(), 15'000, document::TextFill::Auto);
    std::printf("[alphabet bold 15mm] satin=%d tatami=%d contour=%d tatami: %s\n", totals.satin,
                totals.tatami, totals.contour, totals.tatami_letters.c_str());
    CHECK(totals.satin + totals.tatami + totals.contour >= 70);
    // La police grasse est majoritairement cousue en satin.
    CHECK(totals.satin > totals.tatami);
}

TEST_CASE("alphabet in a regular sans-serif is fully sewn at 20 mm") {
    const auto totals = run_alphabet(vera(), 20'000, document::TextFill::Auto);
    std::printf("[alphabet regular 20mm] satin=%d tatami=%d contour=%d tatami: %s\n", totals.satin,
                totals.tatami, totals.contour, totals.tatami_letters.c_str());
    CHECK(totals.satin + totals.tatami + totals.contour >= 70);
}

TEST_CASE("alphabet in tatami and contour modes is fully sewn") {
    CHECK(run_alphabet(vera_bold(), 15'000, document::TextFill::Tatami).tatami >= 70);
    CHECK(run_alphabet(vera_bold(), 15'000, document::TextFill::Contour).contour >= 70);
}
