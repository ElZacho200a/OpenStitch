// SPDX-License-Identifier: Apache-2.0
//
// Persistance des remplissages radial, en spirale et croise (HP-STI-008 / 009 / 014) : round-trip
// exact et lecture d'objets sans cle de reglage (defauts du modele).
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <string>

#include "archive.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

document::Project project_with(const document::StitchParams& params) {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.params = params;
    project.embroidery_objects.push_back(emb);
    return project;
}

document::StitchParams round_trip(const document::StitchParams& params, const char* name) {
    const auto path = fs::temp_directory_path() / name;
    REQUIRE(project_io::save_project(path, project_with(params)).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    fs::remove(path);
    return loaded->embroidery_objects[0].params;
}

document::StitchParams from_json_type(const char* type, const char* name) {
    const std::string json =
        R"({"schemaVersion":3,"document":{"mmPerPx":0.5,"objectIdLast":1,"ops":[],)"
        R"("vectorObjects":[],"embroideryObjects":[{"id":1,"name":"e","sourceVector":0,)"
        R"("rgb":[1,2,3],"visible":true,"params":{"type":")" +
        std::string(type) + R"("}}]}})";
    const auto path = fs::temp_directory_path() / name;
    std::map<std::string, project_io::detail::Blob> entries;
    entries.emplace("project.json", project_io::detail::Blob(json.begin(), json.end()));
    REQUIRE(project_io::detail::write_zip(path, entries).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->embroidery_objects.size() == 1);
    fs::remove(path);
    return loaded->embroidery_objects[0].params;
}

} // namespace

TEST_CASE("radial fill: osp round-trip keeps every setting including the centre") {
    document::RadialFillParams p;
    p.row_spacing = Micrometers{550};
    p.stitch_length = Micrometers{2'400};
    p.min_length = Micrometers{900};
    p.inset = Micrometers{350};
    p.center = Vec2um{Micrometers{-1'250}, Micrometers{7'000}};
    p.hidden_underpath = false;
    CHECK(std::get<document::RadialFillParams>(round_trip(p, "os_radial_rt.osp")) == p);
}

TEST_CASE("spiral fill: osp round-trip keeps every setting") {
    document::SpiralFillParams p;
    p.row_spacing = Micrometers{700};
    p.center = Vec2um{Micrometers{100}, Micrometers{200}};
    p.clockwise = true;
    p.hidden_underpath = false;
    CHECK(std::get<document::SpiralFillParams>(round_trip(p, "os_spiral_rt.osp")) == p);
}

TEST_CASE("crosshatch fill: osp round-trip keeps every setting") {
    document::CrosshatchFillParams p;
    p.angle = Angle{0.5};
    p.layers = 3;
    p.row_spacing = Micrometers{2'200};
    p.stitch_length = Micrometers{2'500};
    p.inset = Micrometers{300};
    p.hidden_underpath = false;
    CHECK(std::get<document::CrosshatchFillParams>(round_trip(p, "os_crosshatch_rt.osp")) == p);
}

TEST_CASE("radial spiral crosshatch: missing keys fall back to the model defaults") {
    CHECK(std::get<document::RadialFillParams>(from_json_type("radialFill", "os_radial_def.osp")) ==
          document::RadialFillParams{});
    CHECK(std::get<document::SpiralFillParams>(from_json_type("spiralFill", "os_spiral_def.osp")) ==
          document::SpiralFillParams{});
    CHECK(std::get<document::CrosshatchFillParams>(from_json_type(
              "crosshatchFill", "os_crosshatch_def.osp")) == document::CrosshatchFillParams{});
}
