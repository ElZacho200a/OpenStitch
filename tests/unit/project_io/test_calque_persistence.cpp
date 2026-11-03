// SPDX-License-Identifier: Apache-2.0
//
// HP-OBJ-024 : persistance des calques (schéma v9). Un document sans calque reste écrit dans sa
// version antérieure ; un document avec calques fait l'aller-retour (ordre, nom, visibilité,
// verrou, appartenance des formes).
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

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

} // namespace

TEST_CASE("calques : round-trip de la liste, des drapeaux et de l'appartenance des formes") {
    auto project = base_project();
    document::Calque c1;
    c1.id = project.object_ids.next();
    c1.name = "Broderie principale";
    document::Calque c2;
    c2.id = project.object_ids.next();
    c2.name = "Effets";
    c2.visible = false;
    c2.locked = true;
    project.calques = {c1, c2};
    document::VectorObject shape;
    shape.id = project.object_ids.next();
    shape.calque = c2.id;
    project.vector_objects.push_back(shape);
    document::VectorObject plain;
    plain.id = project.object_ids.next();
    project.vector_objects.push_back(plain);

    const auto path = fs::temp_directory_path() / "openstitch_calques_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    project_io::LoadInfo info;
    const auto loaded = project_io::load_project(path, &info);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(info.fileVersion == project_io::kSchemaVersionCalques);
    REQUIRE(loaded->calques.size() == 2);
    CHECK(loaded->calques[0].name == "Broderie principale");
    CHECK(loaded->calques[0].visible);
    CHECK_FALSE(loaded->calques[0].locked);
    CHECK(loaded->calques[1].name == "Effets");
    CHECK_FALSE(loaded->calques[1].visible);
    CHECK(loaded->calques[1].locked);
    CHECK(loaded->findObject(shape.id)->calque == c2.id);
    CHECK_FALSE(loaded->findObject(plain.id)->calque.valid());
}

TEST_CASE("calques : un document sans calque garde sa version et se relit sans calque") {
    auto project = base_project();
    document::VectorObject shape;
    shape.id = project.object_ids.next();
    project.vector_objects.push_back(shape);
    const auto path = fs::temp_directory_path() / "openstitch_no_calques.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    project_io::LoadInfo info;
    const auto loaded = project_io::load_project(path, &info);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(info.fileVersion == project_io::kSchemaVersion); // lisible par les versions antérieures
    CHECK(loaded->calques.empty());
}
