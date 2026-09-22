// SPDX-License-Identifier: Apache-2.0
//
// Persistance de Project::finishing (Lots E/F, audit marine plein cadre
// 2026-09-22) : coupes automatiques, points d'arrêt, points courts. Bloc
// additif : un .osp antérieur sans ce bloc est relu SANS finitions (séquence
// strictement identique à avant), un projet nouveau les a activées.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <string>

#include "archive.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

TEST_CASE("finishing : round-trip exact via l'API publique") {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    project.finishing.trim_threshold = Micrometers{4'500};
    project.finishing.trim_before_color_change = false;
    project.finishing.lock_type = document::LockStitch::Triangle;
    project.finishing.lock_length = Micrometers{650};
    project.finishing.lock_passes = 3;
    project.finishing.filter_short_stitches = false;
    project.finishing.min_stitch_length = Micrometers{400};

    const auto path = fs::temp_directory_path() / "openstitch_finishing_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->finishing == project.finishing);
    fs::remove(path);
}

TEST_CASE("finishing : projet anterieur sans le bloc -> finitions desactivees") {
    const std::string json =
        R"({"schemaVersion":3,"document":{"mmPerPx":0.5,"objectIdLast":0,"ops":[],)"
        R"("vectorObjects":[],"embroideryObjects":[]}})";
    const auto path = fs::temp_directory_path() / "openstitch_finishing_legacy.osp";
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(json.begin(), json.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());

    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK_FALSE(loaded->finishing.enabled);
    CHECK(loaded->finishing == document::SequenceFinishing::legacy());
    // Un projet NOUVEAU, lui, a les finitions activées.
    CHECK(document::Project{}.finishing.enabled);
    fs::remove(path);
}
