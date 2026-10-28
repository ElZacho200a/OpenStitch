// SPDX-License-Identifier: Apache-2.0
//
// HP-THR-006 / HP-THR-008 : persistance des coloris et du tissu de fond. Clés facultatives du
// projet : un document sans coloris ni tissu garde ses octets (pas de clé), un document avec fait
// l'aller-retour exact.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

document::Project make_project() {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    for (std::uint8_t i = 1; i <= 2; ++i) {
        document::EmbroideryObject e;
        e.id = project.object_ids.next();
        e.rgb = {i, i, i};
        project.embroidery_objects.push_back(e);
    }
    return project;
}

document::Project round_trip(const document::Project& project, const char* name) {
    const auto path = fs::temp_directory_path() / name;
    REQUIRE(project_io::save_project(path, project).has_value());
    auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    return std::move(*loaded);
}

} // namespace

TEST_CASE("colorways : round-trip de la liste, du coloris actif et des fils") {
    auto project = make_project();
    document::Colorway a;
    a.name = "Original";
    a.entries.push_back({project.embroidery_objects[0].id, std::nullopt, {1, 1, 1}});
    a.entries.push_back({project.embroidery_objects[1].id, std::nullopt, {2, 2, 2}});
    document::Colorway b;
    b.name = "Hiver";
    b.entries.push_back({project.embroidery_objects[0].id,
                         thread_palette::ThreadKey{"generic", "G06"},
                         {200, 16, 46}});
    b.entries.push_back({project.embroidery_objects[1].id, std::nullopt, {9, 8, 7}});
    project.colorways = {a, b};
    project.active_colorway = 1;

    const auto loaded = round_trip(project, "openstitch_colorway_roundtrip.osp");
    REQUIRE(loaded.colorways.size() == 2);
    CHECK(loaded.active_colorway == 1);
    CHECK(loaded.colorways[0].name == "Original");
    CHECK(loaded.colorways[1].name == "Hiver");
    CHECK(loaded.colorways[1].entries == b.entries);
    CHECK(loaded.colorways[0].entries == a.entries);
}

TEST_CASE("fabric : round-trip de la couleur et du nom, defaut = pas de cle") {
    auto project = make_project();
    CHECK(project.fabric == document::FabricSettings{});
    const auto plain = round_trip(project, "openstitch_fabric_default.osp");
    CHECK(plain.fabric == document::FabricSettings{});
    CHECK(plain.colorways.empty());
    CHECK(plain.active_colorway == -1);

    project.fabric.color = {30, 40, 120};
    project.fabric.name = "Jean";
    project.fabric.material = "denim";
    project.fabric.stabilizer = "cut-away";
    const auto loaded = round_trip(project, "openstitch_fabric_roundtrip.osp");
    CHECK(loaded.fabric.color == std::array<std::uint8_t, 3>{30, 40, 120});
    CHECK(loaded.fabric.name == "Jean");
    CHECK(loaded.fabric.material == "denim");
    CHECK(loaded.fabric.stabilizer == "cut-away");
}
