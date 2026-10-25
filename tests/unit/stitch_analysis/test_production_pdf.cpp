// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openstitch/stitch_analysis/production_pdf.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

stitch::StitchCommand cmd(Vec2um p, stitch::CommandType t, std::uint64_t src) {
    return {p, t, ObjectId{src}};
}

ProductionSheet sampleSheet(const std::string& notes) {
    using T = stitch::CommandType;
    document::Project project;
    document::EmbroideryObject a;
    a.id = ObjectId{1};
    a.name = "Cercle";
    a.rgb = {200, 10, 10};
    project.embroidery_objects = {a};
    stitch::StitchSequence seq;
    seq.commands = {
        cmd(um(0, 0), T::Jump, 1),          cmd(um(0, 0), T::Stitch, 1),
        cmd(um(10'000, 0), T::Stitch, 1),   cmd(um(10'000, 10'000), T::Stitch, 1),
        cmd(um(10'000, 10'000), T::End, 0),
    };
    ProductionOptions opts;
    opts.project_name = "Ecusson (a)\\b";
    opts.date = "2026-10-24";
    opts.client = "Atelier \xC3\xA9t\xC3\xA9";
    opts.notes = notes;
    return make_production_sheet(project, seq, opts);
}

} // namespace

TEST_CASE("fiche de production PDF : structure valide et deterministe") {
    const auto sheet = sampleSheet("Notes de test");
    const auto pdf = production_to_pdf(sheet);
    REQUIRE(pdf.rfind("%PDF-1.4", 0) == 0);
    CHECK(pdf.find("%%EOF") != std::string::npos);
    CHECK(pdf.find("/Type /Catalog") != std::string::npos);
    CHECK(pdf.find("/Count 1") != std::string::npos);
    CHECK(pdf.find("Fiche de production") != std::string::npos);
    // Parenthese et antislash echappes, accent en octal WinAnsi (e aigu = 351).
    CHECK(pdf.find("Ecusson \\(a\\)\\\\b") != std::string::npos);
    CHECK(pdf.find("\\351") != std::string::npos);
    CHECK(pdf == production_to_pdf(sheet));

    // startxref pointe sur la table xref.
    const auto sx = pdf.rfind("startxref\n");
    REQUIRE(sx != std::string::npos);
    const auto offset = std::stoul(pdf.substr(sx + 10));
    CHECK(pdf.compare(offset, 4, "xref") == 0);
}

TEST_CASE("fiche de production PDF : de longues notes ouvrent une seconde page") {
    std::string notes;
    for (int i = 0; i < 400; ++i) {
        notes += "ligne de notes numero " + std::to_string(i) + "\n";
    }
    const auto pdf = production_to_pdf(sampleSheet(notes));
    CHECK(pdf.find("/Count 1") == std::string::npos);
    CHECK(pdf.find("/Count ") != std::string::npos); // 400 lignes : plusieurs pages
}

TEST_CASE("fiche de production PDF : fiche vide sans plantage") {
    const ProductionSheet empty;
    const auto pdf = production_to_pdf(empty);
    CHECK(pdf.rfind("%PDF-1.4", 0) == 0);
    CHECK(pdf.find("%%EOF") != std::string::npos);
}
