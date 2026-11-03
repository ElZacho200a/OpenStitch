// SPDX-License-Identifier: Apache-2.0
// HP-STI-003 : persistance .osp du blanket stitch / zigzag.
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

fs::path unique_path(const char* stem) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / (std::string(stem) + std::to_string(unique) + ".osp");
}

document::Project base_project() {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    return project;
}

} // namespace

TEST_CASE("blanket : round-trip .osp des reglages") {
    auto project = base_project();
    document::BlanketStitchParams p;
    p.style = document::BlanketStyle::Zigzag;
    p.width = Micrometers{4'500};
    p.spacing = Micrometers{1'200};
    p.side = document::BlanketSide::Center;
    document::EmbroideryObject e;
    e.id = ObjectId{10};
    e.name = "b";
    e.params = p;
    project.embroidery_objects.push_back(e);

    const auto path = unique_path("openstitch_blanket_rt_");
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    CHECK(loaded->embroidery_objects[0].params == document::StitchParams{p});
    CHECK(loaded->embroidery_objects[0].is_blanket());
}

TEST_CASE("blanket : cles absentes -> defauts du modele") {
    const std::string json =
        R"({"schemaVersion":3,"document":{"mmPerPx":0.5,"objectIdLast":9,"ops":[],)"
        R"("vectorObjects":[],"embroideryObjects":[{"id":2,"name":"b","sourceVector":1,)"
        R"("rgb":[1,2,3],"params":{"type":"blanket"}}]}})";
    const auto path = unique_path("openstitch_blanket_legacy_");
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(json.begin(), json.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    CHECK(loaded->embroidery_objects[0].params ==
          document::StitchParams{document::BlanketStitchParams{}});
}
