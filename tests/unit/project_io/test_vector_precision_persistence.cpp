// SPDX-License-Identifier: Apache-2.0
//
// Surcharges de précision de vectorisation par région (VEC-ADAPT-01, lot B) : aller-retour exact
// dans le .osp ; sans surcharge, aucune clé n'est écrite.
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

TEST_CASE("precision de region : aller-retour exact dans le projet") {
    auto project = make_project();
    project.vector_precision[RegionId{3}] = document::VectorPrecision::Maximum;
    project.vector_precision[RegionId{7}] = document::VectorPrecision::Simplify;
    project.vector_precision[RegionId{9}] = document::VectorPrecision::Standard;
    const auto loaded = round_trip(project, "openstitch_vector_precision_roundtrip.osp");
    CHECK(loaded.vector_precision == project.vector_precision);
}

TEST_CASE("precision de region : absente = aucune surcharge au rechargement") {
    const auto loaded = round_trip(make_project(), "openstitch_vector_precision_none.osp");
    CHECK(loaded.vector_precision.empty());
}
