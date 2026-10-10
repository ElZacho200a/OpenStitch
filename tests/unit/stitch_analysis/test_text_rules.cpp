// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_analysis/text_rules.hpp"

using namespace openstitch;

namespace {

// Projet à un texte et une lettre (le propriétaire est porté par le vecteur source).
document::Project project_with_text(std::int32_t cap_um, document::TextFill fill,
                                    document::StitchParams params) {
    document::Project project;
    document::TextObject text;
    text.id = project.object_ids.next();
    text.cap_height = Micrometers{cap_um};
    text.fill = fill;
    text.origin = {Micrometers{1'000}, Micrometers{2'000}};
    project.text_objects.push_back(text);

    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.text_owner = text.id;
    project.vector_objects.push_back(vec);

    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = std::move(params);
    project.embroidery_objects.push_back(emb);
    return project;
}

} // namespace

TEST_CASE("text rule: big enough text raises nothing") {
    const auto project =
        project_with_text(12'000, document::TextFill::Auto, document::AutoSatinParams{});
    CHECK(stitch_analysis::analyze_text_objects(project).empty());
}

TEST_CASE("text rule: under 5 mm satin is fragile, under 3 mm unreadable") {
    auto project = project_with_text(4'000, document::TextFill::Auto, document::AutoSatinParams{});
    auto findings = stitch_analysis::analyze_text_objects(project);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].category == "texte-trop-petit");
    CHECK(findings[0].object == project.embroidery_objects[0].id);
    CHECK(findings[0].location == project.text_objects[0].origin);
    CHECK(findings[0].message.find("fragile") != std::string::npos);

    project = project_with_text(2'500, document::TextFill::Auto, document::AutoSatinParams{});
    findings = stitch_analysis::analyze_text_objects(project);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].message.find("illisible") != std::string::npos);

    // Un contour ne craint pas la fragilité du satin (seul « illisible » compte).
    project =
        project_with_text(4'000, document::TextFill::Contour, document::RunningStitchParams{});
    CHECK(stitch_analysis::analyze_text_objects(project).empty());
}

TEST_CASE("text rule: satin text sewn as contour is reported") {
    const auto project =
        project_with_text(12'000, document::TextFill::Satin, document::RunningStitchParams{});
    const auto findings = stitch_analysis::analyze_text_objects(project);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].category == "trait-trop-fin");
}

TEST_CASE("text rule: plain objects are ignored") {
    document::Project project;
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    project.embroidery_objects.push_back(emb);
    CHECK(stitch_analysis::analyze_text_objects(project).empty());
}
