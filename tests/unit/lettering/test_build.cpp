// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <variant>

#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "test_support.hpp"

using namespace lettering_test;

namespace {

lettering::TextBuild build(const lettering::Font& font, const document::TextObject& t,
                           IdGenerator<ObjectId>& ids, bool verify = true) {
    lettering::BuildOptions options;
    options.verify_satin = verify;
    auto r = lettering::build_text_objects(font, t, ids, options);
    REQUIRE(r.has_value());
    return std::move(*r);
}

bool has_warning(const lettering::TextBuild& b, const std::string& code) {
    return std::any_of(b.warnings.begin(), b.warnings.end(),
                       [&](const auto& w) { return w.code == code; });
}

document::Project project_of(const document::TextObject& t, lettering::TextBuild b) {
    document::Project p;
    p.text_objects.push_back(t);
    p.vector_objects = std::move(b.vectors);
    p.embroidery_objects = std::move(b.embroideries);
    return p;
}

} // namespace

TEST_CASE("each letter becomes one vector object plus one embroidery object") {
    IdGenerator<ObjectId> ids;
    ids.reset(1); // l'id 1 est celui du texte
    auto t = make_text("HELLO");
    const auto b = build(vera_bold(), t, ids);
    REQUIRE(b.vectors.size() == 5);
    REQUIRE(b.embroideries.size() == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(b.vectors[i].text_owner == t.id);
        CHECK(b.embroideries[i].source_vector == b.vectors[i].id);
        CHECK(b.embroideries[i].intent == document::EmbroideryIntent::ForcedUserChoice);
        CHECK(b.embroideries[i].rgb == t.rgb);
    }
    // Ids frais, distincts, et alloués par l'appelant.
    CHECK(ids.last() == 1 + 10);
}

TEST_CASE("fill kind follows the text setting and the stroke width") {
    IdGenerator<ObjectId> ids;
    auto t = make_text("HI", 12'000);

    t.fill = document::TextFill::Contour;
    for (const auto& e : build(vera_bold(), t, ids).embroideries) {
        CHECK(std::holds_alternative<document::RunningStitchParams>(e.params));
    }
    t.fill = document::TextFill::Tatami;
    for (const auto& e : build(vera_bold(), t, ids).embroideries) {
        CHECK(e.is_tatami());
    }
    // Vera Gras à 12 mm : traits d'environ 2,5 mm -> satin.
    t.fill = document::TextFill::Auto;
    for (const auto& e : build(vera_bold(), t, ids).embroideries) {
        CHECK(e.is_auto_satin());
    }
}

TEST_CASE("a glyph too wide for satin falls back to tatami with a warning") {
    IdGenerator<ObjectId> ids;
    auto t = make_text("H", 60'000); // traits ~12 mm
    t.fill = document::TextFill::Satin;
    auto b = build(vera_bold(), t, ids);
    REQUIRE(b.embroideries.size() == 1);
    CHECK(b.embroideries[0].is_tatami());
    CHECK(has_warning(b, "lettre-trop-large"));

    // En mode Auto, le tatami est le choix normal : pas d'avertissement de repli.
    t.fill = document::TextFill::Auto;
    b = build(vera_bold(), t, ids);
    CHECK(b.embroideries[0].is_tatami());
    CHECK_FALSE(has_warning(b, "lettre-trop-large"));
}

TEST_CASE("strokes under 1 mm are sewn as contour with a warning") {
    IdGenerator<ObjectId> ids;
    auto t = make_text("l", 6'000); // Vera maigre : trait ~0,7 mm
    t.fill = document::TextFill::Satin;
    const auto b = build(vera(), t, ids);
    REQUIRE(b.embroideries.size() == 1);
    CHECK(std::holds_alternative<document::RunningStitchParams>(b.embroideries[0].params));
    CHECK(has_warning(b, "trait-trop-fin"));
}

TEST_CASE("text under the minimum height triggers the size warning") {
    IdGenerator<ObjectId> ids;
    CHECK(has_warning(build(vera_bold(), make_text("AB", 4'000), ids), "texte-trop-petit"));
    CHECK(has_warning(build(vera_bold(), make_text("AB", 2'000), ids), "texte-trop-petit"));
    CHECK_FALSE(has_warning(build(vera_bold(), make_text("AB", 8'000), ids), "texte-trop-petit"));
    const auto small = lettering::check_text_size(make_text("AB", 2'000));
    REQUIRE(small.size() == 1);
    CHECK(small[0].message.find("illisible") != std::string::npos);
}

TEST_CASE("small letters drop pull compensation and the centre underlay when thin") {
    IdGenerator<ObjectId> ids;
    auto t = make_text("H", 7'000);
    const auto b = build(vera_bold(), t, ids);
    const auto* sat = std::get_if<document::AutoSatinParams>(&b.embroideries[0].params);
    REQUIRE(sat != nullptr);
    CHECK(sat->pull_compensation.value == 0);
    t.cap_height = Micrometers{20'000};
    const auto big = build(vera_bold(), t, ids);
    const auto* sat2 = std::get_if<document::AutoSatinParams>(&big.embroideries[0].params);
    REQUIRE(sat2 != nullptr);
    CHECK(sat2->pull_compensation.value > 0);
    CHECK(sat2->center_underlay);
}

TEST_CASE("small letters get a looser density, capped at plus 15 percent") {
    IdGenerator<ObjectId> ids;
    const auto spacing_at = [&](std::int32_t cap_um) {
        const auto b = build(vera_bold(), make_text("H", cap_um), ids);
        const auto* sat = std::get_if<document::AutoSatinParams>(&b.embroideries[0].params);
        REQUIRE(sat != nullptr);
        return sat->spacing.value;
    };
    CHECK(spacing_at(12'000) == 400); // au-dessus de 8 mm : réglage inchangé
    CHECK(spacing_at(8'000) == 400);  // seuil exclu
    CHECK(spacing_at(6'000) == 430);  // mi-parcours : +7,5 %
    // Plein effet (+15 %) à 4 mm et en dessous, plafonné : vérifié en tatami, car le trait
    // d'une capitale de 4 mm est déjà sous 1 mm (cousue en contour).
    for (const std::int32_t cap : {4'000, 3'500}) {
        auto t = make_text("H", cap);
        t.fill = document::TextFill::Tatami;
        const auto b = build(vera_bold(), t, ids);
        const auto* tat = std::get_if<document::TatamiParams>(&b.embroideries[0].params);
        REQUIRE(tat != nullptr);
        CHECK(tat->row_spacing.value == 460);
    }
    // Le réglage de l'utilisateur est le plancher : jamais plus serré que demandé.
    auto t = make_text("H", 7'000);
    t.density = Micrometers{500};
    const auto b = build(vera_bold(), t, ids);
    const auto* sat = std::get_if<document::AutoSatinParams>(&b.embroideries[0].params);
    REQUIRE(sat != nullptr);
    CHECK(sat->spacing.value >= 500);
}

TEST_CASE("small tatami letters use shorter stitches and the adjusted row spacing") {
    IdGenerator<ObjectId> ids;
    auto t = make_text("H", 6'000);
    t.fill = document::TextFill::Tatami;
    const auto small = build(vera_bold(), t, ids);
    const auto* tat = std::get_if<document::TatamiParams>(&small.embroideries[0].params);
    REQUIRE(tat != nullptr);
    CHECK(tat->stitch_length.value == 2'000);
    CHECK(tat->row_spacing.value == 430);
    CHECK(tat->inset.value == 0);
    CHECK_FALSE(tat->underlay_edge);

    t.cap_height = Micrometers{12'000};
    const auto big = build(vera_bold(), t, ids);
    const auto* tat2 = std::get_if<document::TatamiParams>(&big.embroideries[0].params);
    REQUIRE(tat2 != nullptr);
    CHECK(tat2->stitch_length.value == 3'000);
    CHECK(tat2->row_spacing.value == 400);

    // Très petit : la longueur de point ne descend pas sous 1,5 mm.
    t.cap_height = Micrometers{3'000};
    const auto tiny = build(vera_bold(), t, ids);
    const auto* tat3 = std::get_if<document::TatamiParams>(&tiny.embroideries[0].params);
    REQUIRE(tat3 != nullptr);
    CHECK(tat3->stitch_length.value == 1'500);
}

TEST_CASE("small text renders: stitches exist, stay in the text box and are deterministic") {
    IdGenerator<ObjectId> ids;
    ids.reset(1);
    auto t = make_text("Hi 7", 6'000);
    const auto b = build(vera_bold(), t, ids);
    auto project = project_of(t, b);
    const auto seq1 = stitch_generation::effective_sequence(project);
    const auto seq2 = stitch_generation::effective_sequence(project);
    REQUIRE(seq1.has_value());
    REQUIRE(seq2.has_value());
    CHECK(seq1->commands == seq2->commands);
    CHECK(seq1->commands.size() > 40);
    for (const auto& c : seq1->commands) {
        CHECK(c.pos.x.value > -1'500);
        CHECK(c.pos.x.value < b.layout.width.value + 1'500);
        CHECK(c.pos.y.value > -1'500);
        CHECK(c.pos.y.value < 6'000 + 1'500);
    }
    for (const auto& f : stitch_analysis::analyze(*seq1)) {
        CHECK(f.category != "saut-long");
    }
}

TEST_CASE("manual kerning flows through the build options") {
    IdGenerator<ObjectId> ids;
    auto t = make_text("AV", 12'000);
    t.kerning = false;
    lettering::BuildOptions options;
    options.verify_satin = false;
    const auto plain = lettering::build_text_objects(vera_bold(), t, ids, options);
    options.layout.kerning_overrides.push_back({U'A', U'V', Micrometers{-2'000}});
    const auto tight = lettering::build_text_objects(vera_bold(), t, ids, options);
    REQUIRE(plain.has_value());
    REQUIRE(tight.has_value());
    CHECK(tight->layout.glyphs[1].pen.x.value - plain->layout.glyphs[1].pen.x.value == -2'000);
}

TEST_CASE("generated text sews: points exist, stay near the letters and are deterministic") {
    IdGenerator<ObjectId> ids;
    ids.reset(1);
    auto t = make_text("Abc 12", 14'000);
    t.origin = {Micrometers{-20'000}, Micrometers{0}};
    const auto b = build(vera_bold(), t, ids);
    auto project = project_of(t, b);

    const auto seq1 = stitch_generation::effective_sequence(project);
    const auto seq2 = stitch_generation::effective_sequence(project);
    REQUIRE(seq1.has_value());
    REQUIRE(seq2.has_value());
    CHECK(seq1->commands == seq2->commands);
    CHECK(seq1->commands.size() > 200);

    // Aucune piqûre ne s'éloigne de la boîte du texte (marge d'une lettre).
    for (const auto& c : seq1->commands) {
        CHECK(c.pos.x.value > -20'000 - 3'000);
        CHECK(c.pos.x.value < -20'000 + b.layout.width.value + 3'000);
        CHECK(c.pos.y.value > -3'500);
        CHECK(c.pos.y.value < 14'000 + 3'500);
    }
    // L'analyse ne voit ni point long ni saut aberrant dans un texte standard.
    const auto findings = stitch_analysis::analyze(*seq1);
    for (const auto& f : findings) {
        CHECK(f.category != "saut-long");
    }
}

TEST_CASE("build is deterministic and ids are stable per call order") {
    auto t = make_text("Repete", 10'000);
    IdGenerator<ObjectId> a;
    IdGenerator<ObjectId> b;
    const auto r1 = build(vera_bold(), t, a);
    const auto r2 = build(vera_bold(), t, b);
    REQUIRE(r1.vectors.size() == r2.vectors.size());
    for (std::size_t i = 0; i < r1.vectors.size(); ++i) {
        CHECK(r1.vectors[i].paths == r2.vectors[i].paths);
        CHECK(r1.embroideries[i].params == r2.embroideries[i].params);
    }
}
