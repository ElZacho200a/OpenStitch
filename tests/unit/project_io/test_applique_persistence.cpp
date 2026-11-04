// SPDX-License-Identifier: Apache-2.0
// HP-SPEC-001 : persistance .osp de l'objet appliqué.
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

fs::path write_json(const char* stem, const std::string& json) {
    const auto path = unique_path(stem);
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(json.begin(), json.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    return path;
}

} // namespace

TEST_CASE("applique : round-trip .osp des reglages") {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    document::AppliqueParams p;
    p.placement_length = Micrometers{2'500};
    p.tack = document::AppliqueTack::Blanket;
    p.tack_width = Micrometers{1'800};
    p.tack_spacing = Micrometers{2'400};
    p.cover_width = Micrometers{4'000};
    p.cover_density = Micrometers{350};
    p.cut_offset = Micrometers{-750};
    p.stops = false;
    document::EmbroideryObject e;
    e.id = ObjectId{10};
    e.name = "a";
    e.params = p;
    project.embroidery_objects.push_back(e);

    const auto path = unique_path("openstitch_applique_rt_");
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    CHECK(loaded->embroidery_objects[0].params == document::StitchParams{p});
    CHECK(loaded->embroidery_objects[0].is_applique());
}

TEST_CASE("applique : cles absentes -> defauts du modele") {
    const auto path =
        write_json("openstitch_applique_legacy_",
                   R"({"schemaVersion":3,"document":{"mmPerPx":0.5,"objectIdLast":9,"ops":[],)"
                   R"("vectorObjects":[],"embroideryObjects":[{"id":2,"name":"a","sourceVector":1,)"
                   R"("rgb":[1,2,3],"params":{"type":"applique"}}]}})");
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    CHECK(loaded->embroidery_objects[0].params ==
          document::StitchParams{document::AppliqueParams{}});
}

TEST_CASE("applique : valeurs hors plage bornees, point de maintien invalide refuse") {
    const auto bounded = write_json(
        "openstitch_applique_clamp_",
        R"({"schemaVersion":3,"document":{"mmPerPx":0.5,"objectIdLast":9,"ops":[],)"
        R"("vectorObjects":[],"embroideryObjects":[{"id":2,"name":"a","sourceVector":1,)"
        R"("rgb":[1,2,3],"params":{"type":"applique","coverWidth":99999999,"cutOffset":-99999999}}]}})");
    const auto loaded = project_io::load_project(bounded);
    fs::remove(bounded);
    REQUIRE(loaded.has_value());
    const auto* p = std::get_if<document::AppliqueParams>(&loaded->embroidery_objects[0].params);
    REQUIRE(p != nullptr);
    CHECK(p->cover_width.value == 20'000);
    CHECK(p->cut_offset.value == -5'000);

    const auto bad =
        write_json("openstitch_applique_bad_",
                   R"({"schemaVersion":3,"document":{"mmPerPx":0.5,"objectIdLast":9,"ops":[],)"
                   R"("vectorObjects":[],"embroideryObjects":[{"id":2,"name":"a","sourceVector":1,)"
                   R"("rgb":[1,2,3],"params":{"type":"applique","tack":9}}]}})");
    const auto refused = project_io::load_project(bad);
    fs::remove(bad);
    CHECK_FALSE(refused.has_value());
}
