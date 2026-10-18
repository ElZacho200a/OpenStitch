// SPDX-License-Identifier: Apache-2.0
// Typographie fine d'un texte (schema v8, HP-TXT-007 / HP-TXT-012) : crenage manuel par paire
// et ligatures. Un fichier sans ces reglages doit etre ecrit et relu exactement comme avant.
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

fs::path unique_path(const std::string& stem) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / (stem + std::to_string(unique) + ".osp");
}

document::Project project_with_text(document::TextObject text) {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    text.id = ObjectId{5};
    text.text = "AVOID";
    project.text_objects.push_back(std::move(text));
    return project;
}

std::string json_entry_of(const fs::path& path) {
    const auto entries = project_io::detail::read_zip(path);
    REQUIRE(entries.has_value());
    const auto& blob = entries->at("project.json");
    return std::string(blob.begin(), blob.end());
}

} // namespace

TEST_CASE("text typography: kerning pairs and ligatures round-trip in schema v8") {
    document::TextObject t;
    t.kerning_pairs = {{U'A', U'V', Micrometers{-300}}, {U'T', U'o', Micrometers{150}}};
    t.ligatures = true;
    const auto path = unique_path("openstitch_typo_rt_");
    REQUIRE(project_io::save_project(path, project_with_text(t)).has_value());
    project_io::LoadInfo info;
    const auto loaded = project_io::load_project(path, &info);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(info.fileVersion == project_io::kSchemaVersionTextTypography);
    CHECK_FALSE(info.migrated);
    REQUIRE(loaded->text_objects.size() == 1);
    CHECK(loaded->text_objects[0].kerning_pairs == t.kerning_pairs);
    CHECK(loaded->text_objects[0].ligatures);
}

TEST_CASE("text typography: default settings keep the previous file bytes and version") {
    const auto path = unique_path("openstitch_typo_default_");
    REQUIRE(project_io::save_project(path, project_with_text({})).has_value());
    project_io::LoadInfo info;
    const auto loaded = project_io::load_project(path, &info);
    const std::string json = json_entry_of(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(info.fileVersion == project_io::kSchemaVersion);
    // Aucune cle nouvelle n'apparait : les anciens fichiers se reecrivent a l'identique.
    CHECK(json.find("kerningPairs") == std::string::npos);
    CHECK(json.find("ligatures") == std::string::npos);
    CHECK(loaded->text_objects[0].kerning_pairs.empty());
    CHECK_FALSE(loaded->text_objects[0].ligatures);
}

TEST_CASE("text typography: a v7 file without the keys loads with defaults") {
    const std::string json = R"({"schemaVersion":7,"document":{"mmPerPx":0.5,"objectIdLast":3,)"
                             R"("ops":[],"textObjects":[{"id":1,"text":"Hi"}],)"
                             R"("vectorObjects":[],"embroideryObjects":[]}})";
    const auto path = unique_path("openstitch_typo_v7_");
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(json.begin(), json.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    project_io::LoadInfo info;
    const auto loaded = project_io::load_project(path, &info);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(info.fileVersion == 7);
    REQUIRE(loaded->text_objects.size() == 1);
    CHECK(loaded->text_objects[0].kerning_pairs.empty());
    CHECK_FALSE(loaded->text_objects[0].ligatures);
}

TEST_CASE("text typography: invalid kerning pairs are ignored, not fatal") {
    const std::string json =
        R"({"schemaVersion":8,"document":{"mmPerPx":0.5,"objectIdLast":3,"ops":[],)"
        R"("textObjects":[{"id":1,"text":"Hi","kerningPairs":[{"left":65,"right":86,"adjust":-200},)"
        R"({"left":0,"right":86,"adjust":5}]}],"vectorObjects":[],"embroideryObjects":[]}})";
    const auto path = unique_path("openstitch_typo_bad_");
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(json.begin(), json.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->text_objects[0].kerning_pairs.size() == 1);
    CHECK(loaded->text_objects[0].kerning_pairs[0].adjust.value == -200);
}
