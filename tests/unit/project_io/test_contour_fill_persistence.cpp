// SPDX-License-Identifier: Apache-2.0
//
// Persistance du remplissage concentrique (HP-STI-007) : round-trip exact du type, et lecture
// d'un objet sans aucune clé de réglage (défauts du modèle, jamais une erreur).
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <string>

#include "archive.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

TEST_CASE("contour fill: osp round-trip keeps every setting") {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);

    document::ContourFillParams params;
    params.row_spacing = Micrometers{550};
    params.stitch_length = Micrometers{2'400};
    params.min_length = Micrometers{900};
    params.inset = Micrometers{350};
    params.spiral = false;
    params.hidden_underpath = false;
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.params = params;
    project.embroidery_objects.push_back(emb);

    const auto path = fs::temp_directory_path() / "openstitch_contour_fill_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    REQUIRE(loaded->embroidery_objects[0].is_contour_fill());
    CHECK(std::get<document::ContourFillParams>(loaded->embroidery_objects[0].params) == params);
    fs::remove(path);
}

TEST_CASE("contour fill: missing keys fall back to the model defaults") {
    const std::string json =
        R"({"schemaVersion":3,"document":{"mmPerPx":0.5,"objectIdLast":1,"ops":[],)"
        R"("vectorObjects":[],"embroideryObjects":[{"id":1,"name":"e","sourceVector":0,)"
        R"("rgb":[1,2,3],"visible":true,"params":{"type":"contourFill"}}]}})";
    const auto path = fs::temp_directory_path() / "openstitch_contour_fill_defaults.osp";
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(json.begin(), json.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    REQUIRE(loaded->embroidery_objects[0].is_contour_fill());
    CHECK(std::get<document::ContourFillParams>(loaded->embroidery_objects[0].params) ==
          document::ContourFillParams{});
    fs::remove(path);
}
