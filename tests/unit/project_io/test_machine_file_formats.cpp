// SPDX-License-Identifier: Apache-2.0
// HP-FMT-002..005 : composition projet <-> fichier machine pour PES, JEF et EXP.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "openstitch/formats/format_registry.hpp"
#include "openstitch/project_io/machine_file.hpp"
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
    emb.rgb = {237, 23, 31}; // rouge : entree PEC 5, entree JEF 33 (227,49,31) la plus proche
    emb.params = document::RunningStitchParams{};
    project.embroidery_objects.push_back(emb);
    return project;
}

std::vector<std::uint8_t> read_all(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
}
} // namespace

TEST_CASE("export/import PES, JEF, EXP : memes points, couleurs du projet pour PES et JEF") {
    const document::Project project = square_project();
    const auto sequence = stitch_generation::effective_sequence(project);
    REQUIRE(sequence.has_value());
    std::size_t original = 0;
    for (const auto& c : sequence->commands) {
        original += c.type == stitch::CommandType::Stitch ? 1 : 0;
    }

    for (const char* id : {"pes", "jef", "exp"}) {
        INFO(id);
        const auto path = fs::temp_directory_path() / (std::string("openstitch_rt.") + id);
        const auto path2 = fs::temp_directory_path() / (std::string("openstitch_rt2.") + id);
        REQUIRE(project_io::export_machine_file(project, id, path).has_value());
        REQUIRE(project_io::export_machine_file(project, id, path2).has_value());
        CHECK(read_all(path) == read_all(path2)); // deterministe

        const auto imported = project_io::import_machine_file(path); // format deduit de l'extension
        REQUIRE(imported.has_value());
        CHECK(imported->source_format == id);
        REQUIRE_FALSE(imported->color_blocks.empty());
        std::size_t stitches = 0;
        for (const auto& c : imported->sequence.commands) {
            stitches += c.type == stitch::CommandType::Stitch ? 1 : 0;
        }
        CHECK(stitches == original);
        const std::string fmt(id);
        if (fmt == "pes") {
            CHECK(imported->color_blocks[0].rgb == std::array<std::uint8_t, 3>{237, 23, 31});
        } else if (fmt == "jef") {
            CHECK(imported->color_blocks[0].rgb ==
                  std::array<std::uint8_t, 3>{227, 49, 31}); // Burnt Orange, le plus proche
        } else {
            CHECK(imported->color_blocks[0].rgb == std::array<std::uint8_t, 3>{0, 0, 0});
        }
        fs::remove(path);
        fs::remove(path2);
    }
}

TEST_CASE("export_machine_file : options machine transmises au codec (nom du motif PES)") {
    const document::Project project = square_project();
    const auto path = fs::temp_directory_path() / "openstitch_export_named.pes";
    formats::MachineExportOptions options;
    options.design_name = "LOGO CLUB";
    REQUIRE(project_io::export_machine_file(project, "pes", path, options).has_value());
    const auto* info = formats::find_format("pes");
    REQUIRE(info != nullptr);
    const auto decoded = info->decode_ex(read_all(path));
    REQUIRE(decoded.has_value());
    CHECK(decoded->name == "LOGO CLUB");
    fs::remove(path);
}
