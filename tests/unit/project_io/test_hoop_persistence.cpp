// SPDX-License-Identifier: Apache-2.0
//
// Persistance de la forme du cadre et du profil machine (HP-HOOP-001/002) : champs additifs,
// un .osp anterieur (rectangle, sans machine) se relit a l'identique.
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

TEST_CASE("hoop persistence: shape, radius and machine round-trip") {
    document::Project project = base_project();
    project.canvas.width = Micrometers{150'000};
    project.canvas.height = Micrometers{60'000};
    project.canvas.margin = Micrometers{3'000};
    project.canvas.shape = document::HoopShape::RoundedRectangle;
    project.canvas.corner_radius = Micrometers{10'000};
    project.machine_id = "generic-multi-6";

    const auto path = fs::temp_directory_path() / "openstitch_hoop_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->canvas == project.canvas);
    CHECK(loaded->machine_id == "generic-multi-6");

    project.canvas.shape = document::HoopShape::Ellipse;
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto ellipse = project_io::load_project(path);
    REQUIRE(ellipse.has_value());
    CHECK(ellipse->canvas.shape == document::HoopShape::Ellipse);
    fs::remove(path);
}

TEST_CASE("hoop persistence: a plain rectangle without machine reads back unchanged") {
    document::Project project = base_project();
    const auto path = fs::temp_directory_path() / "openstitch_hoop_default.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK(loaded->canvas.shape == document::HoopShape::Rectangle);
    CHECK(loaded->canvas.corner_radius.value == 0);
    CHECK(loaded->machine_id.empty());
    fs::remove(path);
}
