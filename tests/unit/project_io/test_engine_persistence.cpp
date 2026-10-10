// SPDX-License-Identifier: Apache-2.0
//
// Persistance des réglages du lot « moteur de points » : compensation du tirage du tatami
// (HP-ENG-001), sous-couche automatique (HP-ENG-002), entrée/sortie automatiques
// (HP-ENG-010), découpage des points longs (HP-ENG-008) et satin de bordure (HP-STI-004).
// Tous additifs : un .osp antérieur est relu avec les défauts du modèle (comportement
// inchangé), les valeurs aberrantes d'un fichier édité à la main sont bornées.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <string>

#include "archive.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

document::Project base_project() {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    return project;
}

document::Project load_from_json(const std::string& embroideryObjectsJson,
                                 const std::string& extraDocumentKeys = {}) {
    const std::string json =
        R"({"schemaVersion":3,"document":{"mmPerPx":0.5,"objectIdLast":9,"ops":[],)"
        R"("vectorObjects":[],"embroideryObjects":[)" +
        embroideryObjectsJson + "]" + extraDocumentKeys + "}}";
    // Nom unique : ctest lance chaque TEST_CASE dans un processus distinct, parfois en parallèle.
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path =
        fs::temp_directory_path() / ("openstitch_engine_legacy_" + std::to_string(unique) + ".osp");
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(json.begin(), json.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    return *loaded;
}

} // namespace

TEST_CASE("moteur : round-trip des nouveaux reglages (tatami, directionnel, satin, auto-satin)") {
    auto project = base_project();
    project.finishing.split_long_stitches = true;
    project.finishing.max_stitch_length = Micrometers{6'000};
    project.finishing.auto_join = true;

    document::TatamiParams tatami;
    tatami.pull_compensation = Micrometers{350};
    tatami.underlay_mode = document::UnderlayMode::Auto;
    document::DirectionalFillParams directional;
    directional.underlay_mode = document::UnderlayMode::Auto;
    document::SatinParams satin;
    satin.underlay_mode = document::UnderlayMode::Auto;
    satin.border = document::BorderSatinSpec{Micrometers{4'500}, document::BorderSide::Outside,
                                             document::BorderCorner::Round, 2, 1};
    document::AutoSatinParams autoSatin;
    autoSatin.underlay_mode = document::UnderlayMode::Auto;

    std::uint64_t id = 10;
    for (const document::StitchParams& params :
         std::vector<document::StitchParams>{tatami, directional, satin, autoSatin}) {
        document::EmbroideryObject e;
        e.id = ObjectId{id++};
        e.name = "o";
        e.params = params;
        e.join = (id % 2) ? document::JoinMode::Auto : document::JoinMode::Off;
        project.embroidery_objects.push_back(e);
    }

    const auto path = fs::temp_directory_path() / "openstitch_engine_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->finishing == project.finishing);
    REQUIRE(loaded->embroidery_objects.size() == project.embroidery_objects.size());
    for (std::size_t i = 0; i < project.embroidery_objects.size(); ++i) {
        CHECK(loaded->embroidery_objects[i].params == project.embroidery_objects[i].params);
        CHECK(loaded->embroidery_objects[i].join == project.embroidery_objects[i].join);
    }
    // Deux sauvegardes successives du même projet sont identiques (déterminisme).
    const auto second = fs::temp_directory_path() / "openstitch_engine_roundtrip2.osp";
    REQUIRE(project_io::save_project(second, *loaded).has_value());
    const auto again = project_io::load_project(second);
    REQUIRE(again.has_value());
    CHECK(again->finishing == loaded->finishing);
    fs::remove(path);
    fs::remove(second);
}

TEST_CASE("moteur : un .osp anterieur est relu avec les defauts (comportement inchange)") {
    const auto project = load_from_json(
        R"({"id":2,"name":"t","sourceVector":1,"rgb":[1,2,3],"params":{"type":"tatami",)"
        R"("angle":0,"rowSpacing":400,"stitchLength":3000,"inset":200,"stagger":2}},)"
        R"({"id":3,"name":"s","sourceVector":1,"rgb":[1,2,3],"params":{"type":"satin",)"
        R"("railA":{"closed":false,"nodes":[]},"railB":{"closed":false,"nodes":[]},)"
        R"("density":400,"pullCompensation":0,"centerUnderlay":true,"maxWidth":9000}},)"
        R"({"id":4,"name":"a","sourceVector":1,"rgb":[1,2,3],"params":{"type":"autoSatin"}})",
        R"(,"finishing":{"enabled":true})");
    REQUIRE(project.embroidery_objects.size() == 3);
    const auto& tatami = std::get<document::TatamiParams>(project.embroidery_objects[0].params);
    CHECK(tatami.pull_compensation.value == 0);
    CHECK(tatami.underlay_mode == document::UnderlayMode::Manual);
    const auto& satin = std::get<document::SatinParams>(project.embroidery_objects[1].params);
    CHECK(satin.underlay_mode == document::UnderlayMode::Manual);
    CHECK_FALSE(satin.border.has_value());
    const auto& autoSatin =
        std::get<document::AutoSatinParams>(project.embroidery_objects[2].params);
    CHECK(autoSatin.underlay_mode == document::UnderlayMode::Manual);
    for (const auto& e : project.embroidery_objects) {
        CHECK(e.join == document::JoinMode::Inherit);
    }
    // Bloc finishing présent mais sans les nouvelles clés : désactivées.
    CHECK_FALSE(project.finishing.split_long_stitches);
    CHECK_FALSE(project.finishing.auto_join);
    CHECK(project.finishing.max_stitch_length.value == 7'000);
}

TEST_CASE("moteur : projet sans bloc finishing -> finitions legacy, aucune nouveaute active") {
    const auto project = load_from_json("");
    CHECK(project.finishing == document::SequenceFinishing::legacy());
    CHECK_FALSE(project.finishing.auto_join);
    CHECK_FALSE(project.finishing.split_long_stitches);
}

TEST_CASE("moteur : valeurs aberrantes d'un fichier edite a la main sont bornees") {
    const auto project = load_from_json(
        R"({"id":2,"name":"t","sourceVector":1,"rgb":[1,2,3],"join":9,"params":{"type":"tatami",)"
        R"("angle":0,"rowSpacing":400,"stitchLength":3000,"inset":200,"stagger":2,)"
        R"("pullCompensation":999999,"underlayMode":7}},)"
        R"({"id":3,"name":"n","sourceVector":1,"rgb":[1,2,3],"params":{"type":"tatami",)"
        R"("angle":0,"rowSpacing":400,"stitchLength":3000,"inset":200,"stagger":2,)"
        R"("pullCompensation":-50}},)"
        R"({"id":4,"name":"s","sourceVector":1,"rgb":[1,2,3],"params":{"type":"satin",)"
        R"("railA":{"closed":false,"nodes":[]},"railB":{"closed":false,"nodes":[]},)"
        R"("density":400,"pullCompensation":0,"centerUnderlay":true,"maxWidth":9000,)"
        R"("border":{"width":10,"side":9,"corner":-3,"pathSet":-4,"ring":5}}})",
        R"(,"finishing":{"enabled":true,"maxStitchLength":99999999,"autoJoin":true})");
    const auto& big = std::get<document::TatamiParams>(project.embroidery_objects[0].params);
    CHECK(big.pull_compensation.value == 3'000);
    CHECK(big.underlay_mode == document::UnderlayMode::Auto);
    CHECK(project.embroidery_objects[0].join == document::JoinMode::Off);
    const auto& negative = std::get<document::TatamiParams>(project.embroidery_objects[1].params);
    CHECK(negative.pull_compensation.value == 0);
    const auto& satin = std::get<document::SatinParams>(project.embroidery_objects[2].params);
    REQUIRE(satin.border.has_value());
    CHECK(satin.border->width.value == 500);
    CHECK(satin.border->side == document::BorderSide::Outside);
    CHECK(satin.border->corner == document::BorderCorner::Sharp);
    CHECK(satin.border->path_set == 0);
    CHECK(satin.border->ring == 5);
    CHECK(project.finishing.max_stitch_length.value == 12'100);
    CHECK(project.finishing.auto_join);
}
