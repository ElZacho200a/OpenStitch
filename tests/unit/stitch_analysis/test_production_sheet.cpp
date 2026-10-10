// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openstitch/stitch_analysis/production_sheet.hpp"
#include "openstitch/thread_palette/catalog.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using Catch::Approx;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

stitch::StitchCommand cmd(Vec2um p, stitch::CommandType t, std::uint64_t src) {
    return {p, t, ObjectId{src}};
}

document::Project twoObjectProject() {
    document::Project project;
    document::EmbroideryObject a;
    a.id = ObjectId{1};
    a.name = "Cercle rouge";
    a.rgb = {200, 10, 10};
    document::EmbroideryObject b;
    b.id = ObjectId{2};
    b.name = "Fond <bleu> & co";
    b.rgb = {10, 20, 220};
    project.embroidery_objects = {a, b};
    return project;
}

// Rouge : 3 points (0,0)->(10,0)->(10,10) ; saut ; bleu : 2 points + coupe.
stitch::StitchSequence twoBlockSequence() {
    using T = stitch::CommandType;
    stitch::StitchSequence seq;
    seq.commands = {
        cmd(um(0, 0), T::Jump, 1),
        cmd(um(0, 0), T::Stitch, 1),
        cmd(um(10'000, 0), T::Stitch, 1),
        cmd(um(10'000, 10'000), T::Stitch, 1),
        cmd(um(10'000, 10'000), T::ColorChange, 0),
        cmd(um(-5'000, -5'000), T::Jump, 2),
        cmd(um(-5'000, -5'000), T::Stitch, 2),
        cmd(um(-5'000, 5'000), T::Stitch, 2),
        cmd(um(-5'000, 5'000), T::Trim, 2),
        cmd(um(-5'000, 5'000), T::End, 0),
    };
    return seq;
}

} // namespace

TEST_CASE("fiche de production : totaux et blocs dans l'ordre de couture") {
    const auto project = twoObjectProject();
    ProductionOptions opts;
    opts.project_name = "Essai";
    opts.date = "2026-10-10";
    const auto sheet = make_production_sheet(project, twoBlockSequence(), opts);

    CHECK(sheet.stitches == 5);
    CHECK(sheet.jumps == 2);
    CHECK(sheet.trims == 1);
    CHECK(sheet.color_changes == 1);
    CHECK(sheet.width_mm == Approx(15.0));
    CHECK(sheet.height_mm == Approx(15.0));
    REQUIRE(sheet.frame_mm);
    CHECK(sheet.frame_mm->first == Approx(100.0));
    CHECK(sheet.fits_frame);

    REQUIRE(sheet.blocks.size() == 2);
    CHECK(sheet.blocks[0].number == 1);
    CHECK(sheet.blocks[0].rgb == std::array<std::uint8_t, 3>{200, 10, 10});
    CHECK(sheet.blocks[0].stitches == 3);
    CHECK(sheet.blocks[0].jumps == 1);
    CHECK(sheet.blocks[0].thread_length_mm == Approx(20.0));
    REQUIRE(sheet.blocks[0].objects.size() == 1);
    CHECK(sheet.blocks[0].objects[0].name == "Cercle rouge");
    CHECK(sheet.blocks[0].thread_label.empty());
    CHECK(sheet.blocks[1].number == 2);
    CHECK(sheet.blocks[1].stitches == 2);
    CHECK(sheet.blocks[1].trims == 1);
    CHECK(sheet.blocks[1].thread_length_mm == Approx(10.0));
    CHECK(sheet.thread_length_m == Approx(0.030));
    CHECK(sheet.preview.size() == 2);
}

TEST_CASE("fiche de production : temps estime selon l'hypothese de vitesse") {
    const auto project = twoObjectProject();
    ProductionOptions opts;
    opts.stitches_per_minute = 500.0;
    opts.color_change_seconds = 60.0;
    opts.trim_seconds = 6.0;
    const auto sheet = make_production_sheet(project, twoBlockSequence(), opts);
    // 5 points / 500 = 0,01 min ; 1 changement = 1 min ; 1 coupe = 0,1 min.
    CHECK(sheet.estimated_minutes == Approx(0.01 + 1.0 + 0.1));
    CHECK(sheet.stitches_per_minute == Approx(500.0));

    ProductionOptions defaults;
    CHECK(defaults.stitches_per_minute >= 600.0);
    CHECK(defaults.stitches_per_minute <= 800.0);
}

TEST_CASE("fiche de production : formatage des durees") {
    CHECK(format_duration_fr(0.0) == "< 1 min");
    CHECK(format_duration_fr(0.3) == "< 1 min");
    CHECK(format_duration_fr(12.0) == "12 min");
    CHECK(format_duration_fr(65.0) == "1 h 05 min");
}

TEST_CASE("fiche de production : cadre depasse et avertissements") {
    auto project = twoObjectProject();
    project.canvas.width = Micrometers{10'000}; // cadre de 10 mm : le motif (15 mm) dépasse
    project.canvas.height = Micrometers{10'000};
    const auto sheet = make_production_sheet(project, twoBlockSequence(), {});
    CHECK_FALSE(sheet.fits_frame);
    bool outOfFrame = false;
    for (const auto& f : sheet.findings) {
        outOfFrame = outOfFrame || f.category == "hors-cadre";
    }
    CHECK(outOfFrame);
    const auto html = production_to_html(sheet);
    CHECK(html.find("dépasse du cadre") != std::string::npos);

    ProductionOptions noFrame;
    noFrame.use_project_canvas = false;
    const auto free = make_production_sheet(project, twoBlockSequence(), noFrame);
    CHECK_FALSE(free.frame_mm.has_value());
    CHECK(free.fits_frame);
}

TEST_CASE("fiche de production : reference de fil issue du thread_key du bloc") {
    REQUIRE_FALSE(thread_palette::all_charts().empty());
    const auto& chart = thread_palette::all_charts().front();
    REQUIRE_FALSE(chart.threads.empty());
    const auto& thread = chart.threads.front();

    auto project = twoObjectProject();
    const auto seq = twoBlockSequence();
    document::ImportedDesign imported;
    imported.source_format = "test";
    imported.sequence = seq;
    stitch::ColorBlock b1;
    b1.rgb = {1, 2, 3};
    b1.thread_key = thread.key;
    b1.start = 0;
    b1.end = 4;
    stitch::ColorBlock b2;
    b2.rgb = {4, 5, 6};
    b2.start = 5;
    b2.end = 9;
    imported.color_blocks = {b1, b2};
    project.imported_design = imported;

    const auto sheet = make_production_sheet(project, seq, {});
    REQUIRE(sheet.blocks.size() == 2);
    CHECK(sheet.blocks[0].rgb == std::array<std::uint8_t, 3>{1, 2, 3});
    CHECK(sheet.blocks[0].thread_label.find(thread.key.code) != std::string::npos);
    CHECK(sheet.blocks[1].thread_label.empty());
}

TEST_CASE("fiche de production : JSON et HTML deterministes et echappes") {
    const auto project = twoObjectProject();
    ProductionOptions opts;
    opts.project_name = "Mon \"projet\"";
    opts.date = "2026-10-10";
    opts.notes = "ligne 1\nligne <2>";
    const auto a = make_production_sheet(project, twoBlockSequence(), opts);
    const auto b = make_production_sheet(project, twoBlockSequence(), opts);
    CHECK(production_to_json(a) == production_to_json(b));
    CHECK(production_to_html(a) == production_to_html(b));

    const auto json = production_to_json(a);
    CHECK(json.find("\"schema\":1") != std::string::npos);
    CHECK(json.find("\"project\":\"Mon \\\"projet\\\"\"") != std::string::npos);
    CHECK(json.find("\"color\":\"#c80a0a\"") != std::string::npos);
    CHECK(json.find("\"stitches\":5") != std::string::npos);

    const auto html = production_to_html(a);
    CHECK(html.find("Fond &lt;bleu&gt; &amp; co") != std::string::npos);
    CHECK(html.find("ligne 1<br/>ligne &lt;2&gt;") != std::string::npos);
    CHECK(html.find("bgcolor=\"#c80a0a\"") != std::string::npos);
    CHECK(html.find("<svg") != std::string::npos);
    CHECK(html.find("Avertissements") != std::string::npos);

    ProductionHtmlOptions withImage;
    withImage.preview_src = "apercu.png";
    const auto html2 = production_to_html(a, withImage);
    CHECK(html2.find("<img src=\"apercu.png\" alt=\"") != std::string::npos);
    CHECK(html2.find("<svg") == std::string::npos);
}

TEST_CASE("fiche de production : sequence vide") {
    document::Project project;
    const auto sheet = make_production_sheet(project, stitch::StitchSequence{}, {});
    CHECK(sheet.stitches == 0);
    CHECK(sheet.blocks.empty());
    CHECK(sheet.estimated_minutes == Approx(0.0));
    CHECK(production_to_html(sheet).find("Aucun bloc") != std::string::npos);
}
