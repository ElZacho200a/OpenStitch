// SPDX-License-Identifier: Apache-2.0
//
// Zones « détail / aplat » : le masque peint fait l'aller-retour exact dans le .osp ; un projet
// sans zone de détail n'écrit aucune entrée supplémentaire.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

document::Project make_project() {
    document::Project project;
    project.original.width = 12;
    project.original.height = 8;
    project.original.rgba.assign(12 * 8 * 4, 255);
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

TEST_CASE("masque de detail : aller-retour exact dans le projet") {
    auto project = make_project();
    segmentation::paint_detail_disc(project.detail_mask, 12, 8, 6, 4, 3, 255, 0.5);
    REQUIRE(project.detail_mask.any_detail());

    const auto loaded = round_trip(project, "openstitch_detail_mask_roundtrip.osp");
    CHECK(loaded.detail_mask == project.detail_mask);
}

TEST_CASE("masque de detail : absent ou vide = aucun masque au rechargement") {
    auto project = make_project();
    const auto plain = round_trip(project, "openstitch_detail_mask_none.osp");
    CHECK(plain.detail_mask.empty());

    project.detail_mask.width = 12;
    project.detail_mask.height = 8;
    project.detail_mask.weights.assign(96, 0); // peint puis entierement efface
    const auto erased = round_trip(project, "openstitch_detail_mask_erased.osp");
    CHECK_FALSE(erased.detail_mask.any_detail());
}
