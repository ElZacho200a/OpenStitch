// SPDX-License-Identifier: Apache-2.0
//
// Persistance de EmbroideryObject::intent (§21/§24 du plan de refonte satin,
// 2026-08-14) -- distingue une classification automatique (AutoChoice) d'un
// choix explicite de l'utilisateur (ForcedUserChoice), jusqu'ici implicite
// dans le CODE (quel chemin d'appel a créé l'objet), désormais un champ
// persisté. Champ additif : un .osp antérieur sans ce champ doit rester
// lisible et retomber sur AutoChoice (comportement historique implicite).
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <string>

#include "archive.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

fs::path write_raw_osp(const std::string& name, const std::string& jsonText) {
    const auto path = fs::temp_directory_path() / name;
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(jsonText.begin(), jsonText.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    return path;
}

std::string document_json_without_intent_field() {
    return R"({"schemaVersion":3,"document":{"mmPerPx":0.5,"objectIdLast":1,"ops":[],)"
           R"("vectorObjects":[],"embroideryObjects":[{"id":1,"name":"e","sourceVector":0,)"
           R"("rgb":[1,2,3],"visible":true,)"
           R"("params":{"type":"running","stitchLength":2500,"minLength":400,"repeats":1}}]}})";
}

}  // namespace

TEST_CASE("intent : round-trip exact pour ForcedUserChoice et AutoChoice via l'API publique") {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);

    document::EmbroideryObject forced;
    forced.id = project.object_ids.next();
    forced.params = document::RunningStitchParams{};
    forced.intent = document::EmbroideryIntent::ForcedUserChoice;
    project.embroidery_objects.push_back(forced);

    document::EmbroideryObject automatic;
    automatic.id = project.object_ids.next();
    automatic.params = document::TatamiParams{};
    automatic.intent = document::EmbroideryIntent::AutoChoice;
    project.embroidery_objects.push_back(automatic);

    const auto path = fs::temp_directory_path() / "openstitch_intent_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 2);
    CHECK(loaded->embroidery_objects[0].intent == document::EmbroideryIntent::ForcedUserChoice);
    CHECK(loaded->embroidery_objects[1].intent == document::EmbroideryIntent::AutoChoice);
    fs::remove(path);
}

TEST_CASE("intent : absent d'un projet anterieur -> AutoChoice, jamais une erreur ni une supposition differente") {
    const auto path =
        write_raw_osp("openstitch_intent_absent.osp", document_json_without_intent_field());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    CHECK(loaded->embroidery_objects[0].intent == document::EmbroideryIntent::AutoChoice);
    fs::remove(path);
}
