// SPDX-License-Identifier: Apache-2.0
// AI-03b (AD-03) : composition projet <-> fichier machine, réutilisée par le
// desktop et la CLI. Couvre aussi la persistance `.osp` du design importé
// (AD-04, bloc additif de `json_serialize.cpp`).
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {
Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

document::Project square_project() {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    geometry::PathSet set;
    set.outer.closed = true;
    set.outer.nodes = {
        {um(0, 0), geometry::NodeType::Corner, std::nullopt, std::nullopt},
        {um(5'000, 0), geometry::NodeType::Corner, std::nullopt, std::nullopt},
        {um(5'000, 5'000), geometry::NodeType::Corner, std::nullopt, std::nullopt},
        {um(0, 5'000), geometry::NodeType::Corner, std::nullopt, std::nullopt},
    };
    vec.paths = {set};
    project.vector_objects.push_back(vec);

    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.rgb = {10, 20, 30};
    emb.params = document::RunningStitchParams{};
    project.embroidery_objects.push_back(emb);
    return project;
}
} // namespace

TEST_CASE("export_machine_file : format inconnu refuse proprement") {
    const document::Project project = square_project();
    const auto path = fs::temp_directory_path() / "openstitch_export_unknown.xyz";
    const auto result = project_io::export_machine_file(project, "xyz", path);
    CHECK_FALSE(result.has_value());
    CHECK(result.error().category == ErrorCategory::UnsupportedFormat);
}

TEST_CASE("export_machine_file('dst') : memes octets que encode_dst(effective_sequence) direct") {
    const document::Project project = square_project();
    const auto path = fs::temp_directory_path() / "openstitch_export_machine_file.dst";
    REQUIRE(project_io::export_machine_file(project, "dst", path).has_value());

    const auto sequence = stitch_generation::effective_sequence(project);
    REQUIRE(sequence.has_value());
    formats::DstWriteOptions machineOptions;
    machineOptions.trim_jumps_with_movement = true; // coupes visibles par la machine
    const auto expectedBytes = formats::encode_dst(*sequence, machineOptions);
    REQUIRE(expectedBytes.has_value());

    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    std::vector<std::uint8_t> actualBytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
    file.close(); // Windows refuse de supprimer un fichier encore ouvert
    CHECK(actualBytes == *expectedBytes);
    fs::remove(path);
}

TEST_CASE("import_machine_file : format inconnu (extension) refuse proprement") {
    const auto path = fs::temp_directory_path() / "openstitch_import_unknown.xyz";
    {
        std::ofstream f(path, std::ios::binary);
        f << "peu importe";
    }
    const auto result = project_io::import_machine_file(path);
    CHECK_FALSE(result.has_value());
    CHECK(result.error().category == ErrorCategory::UnsupportedFormat);
    fs::remove(path);
}

TEST_CASE("import_machine_file('dst') : design importe avec blocs de couleur par defaut (noir)") {
    const document::Project project = square_project();
    const auto path = fs::temp_directory_path() / "openstitch_import_machine_file.dst";
    REQUIRE(project_io::export_machine_file(project, "dst", path).has_value());

    const auto imported = project_io::import_machine_file(path); // format deduit de l'extension
    REQUIRE(imported.has_value());
    CHECK(imported->source_format == "dst");
    CHECK_FALSE(imported->sequence.commands.empty());
    REQUIRE_FALSE(imported->color_blocks.empty());
    for (const auto& block : imported->color_blocks) {
        CHECK(block.rgb == std::array<std::uint8_t, 3>{0, 0, 0}); // DST n'a pas de vraie couleur
        CHECK_FALSE(block.thread_key.has_value());
    }
    fs::remove(path);
}

TEST_CASE("imported_design : round-trip .osp exact") {
    document::Project project;
    document::ImportedDesign imported;
    imported.source_format = "dst";
    imported.sequence.commands = {
        {um(0, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(1'000, 0), stitch::CommandType::ColorChange, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::Stitch, ObjectId{}},
        {um(2'000, 0), stitch::CommandType::End, ObjectId{}},
    };
    stitch::ColorBlock blockA;
    blockA.rgb = {0, 0, 0};
    blockA.start = 0;
    blockA.end = 2;
    stitch::ColorBlock blockB;
    blockB.rgb = {0, 0, 0};
    blockB.start = 3;
    blockB.end = 4;
    blockB.thread_key = thread_palette::ThreadKey{"madeira_polyneon", "1919"};
    imported.color_blocks = {blockA, blockB};
    project.imported_design = imported;

    const auto path = fs::temp_directory_path() / "openstitch_imported_design_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->imported_design.has_value());
    CHECK(*loaded->imported_design == imported);
    fs::remove(path);
}

TEST_CASE("imported_design : absent d'un projet anterieur -> nullopt, pas une erreur") {
    document::Project project = square_project();
    CHECK_FALSE(project.imported_design.has_value());

    const auto path = fs::temp_directory_path() / "openstitch_no_imported_design.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    REQUIRE(loaded.has_value());
    CHECK_FALSE(loaded->imported_design.has_value());
    fs::remove(path);
}
