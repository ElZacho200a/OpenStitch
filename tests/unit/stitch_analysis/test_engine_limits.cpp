// SPDX-License-Identifier: Apache-2.0
//
// HP-ENG-008 : limites de longueur de point alignées sur les réglages du projet, et
// avertissements pré-export sur les réglages d'objets hors limites.
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_analysis/analyze.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;

namespace {

document::EmbroideryObject object(std::uint64_t id, document::StitchParams params,
                                  bool visible = true) {
    document::EmbroideryObject e;
    e.id = ObjectId{id};
    e.name = "objet " + std::to_string(id);
    e.params = std::move(params);
    e.visible = visible;
    return e;
}

} // namespace

TEST_CASE("options_from_project : suit les finitions du projet") {
    document::Project project;
    project.finishing.min_stitch_length = Micrometers{600};
    project.finishing.max_stitch_length = Micrometers{6'000};
    project.finishing.trim_threshold = Micrometers{4'000};
    const auto options = options_from_project(project);
    CHECK(options.min_stitch.value == 600);
    CHECK(options.max_stitch.value == 6'000);
    CHECK(options.trim_threshold.value == 4'000);
}

TEST_CASE("options_from_project : finitions desactivees -> seuils historiques") {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    project.finishing.max_stitch_length = Micrometers{3'000};
    const auto options = options_from_project(project);
    CHECK(options.max_stitch.value == AnalysisOptions{}.max_stitch.value);
}

TEST_CASE("check_stitch_limits : signale les reglages hors limites, ignore le reste") {
    document::Project project;
    document::TatamiParams longFill;
    longFill.stitch_length = Micrometers{9'000};
    document::RunningStitchParams shortRun;
    shortRun.stitch_length = Micrometers{200};
    document::SatinParams satin;
    satin.max_stitch_length = Micrometers{8'000};
    document::AutoSatinParams autoSatin;
    autoSatin.split_threshold = Micrometers{12'000};
    project.embroidery_objects.push_back(object(1, longFill));
    project.embroidery_objects.push_back(object(2, shortRun));
    project.embroidery_objects.push_back(object(3, satin));
    project.embroidery_objects.push_back(object(4, autoSatin));
    project.embroidery_objects.push_back(object(5, document::TatamiParams{})); // dans les limites
    project.embroidery_objects.push_back(object(6, longFill, /*visible=*/false)); // masqué

    const auto findings = check_stitch_limits(project);
    REQUIRE(findings.size() == 4);
    for (const auto& f : findings) {
        CHECK(f.category == "parametre-hors-limites");
        CHECK(f.severity == Severity::Warning);
        CHECK_FALSE(f.hint.empty());
    }
    CHECK(findings[0].object.value == 1);
    CHECK(findings[1].object.value == 2);
    CHECK(findings[2].object.value == 3);
    CHECK(findings[3].object.value == 4);
    CHECK(findings[0].message.find("9,0 mm") != std::string::npos);

    // Une limite plus large dans le projet fait disparaître l'avertissement.
    project.finishing.max_stitch_length = Micrometers{12'000};
    CHECK(check_stitch_limits(project).size() == 1); // reste le point de 0,2 mm
}
