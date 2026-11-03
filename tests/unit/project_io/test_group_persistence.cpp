// SPDX-License-Identifier: Apache-2.0
//
// HP-OBJ-009 : persistance des groupes (cles facultatives, sans montee de schema). Avec groupes,
// l'imbrication et l'appartenance des formes font l'aller-retour ; sans groupe, rien n'apparait.
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

TEST_CASE("groupes : round-trip de la hierarchie et de l'appartenance des formes") {
    auto project = base_project();
    document::ObjectGroup outer{project.object_ids.next(), "Exterieur", {}};
    document::ObjectGroup inner{project.object_ids.next(), "Interieur", outer.id};
    project.groups = {outer, inner};
    document::VectorObject a;
    a.id = project.object_ids.next();
    a.group = inner.id;
    document::VectorObject b;
    b.id = project.object_ids.next();
    project.vector_objects = {a, b};

    const auto path = fs::temp_directory_path() / "openstitch_groups_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->groups.size() == 2);
    CHECK(loaded->groups[0].name == "Exterieur");
    CHECK_FALSE(loaded->groups[0].parent.valid());
    CHECK(loaded->groups[1].parent == outer.id);
    CHECK(loaded->findObject(a.id)->group == inner.id);
    CHECK_FALSE(loaded->findObject(b.id)->group.valid());
    CHECK(loaded->topGroupOfShape(a.id) == outer.id);
}

TEST_CASE("groupes : un document sans groupe se relit sans groupe") {
    auto project = base_project();
    document::VectorObject a;
    a.id = project.object_ids.next();
    project.vector_objects.push_back(a);
    const auto path = fs::temp_directory_path() / "openstitch_groups_absent.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->groups.empty());
    CHECK_FALSE(loaded->findObject(a.id)->group.valid());
}
