// SPDX-License-Identifier: Apache-2.0
// AI-03b (AD-03) : composition projet <-> fichier machine, réutilisée par le
// desktop et la CLI. Couvre aussi la persistance `.osp` du design importé
// (AD-04, bloc additif de `json_serialize.cpp`).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

TEST_CASE("with_start_point : FirstPoint est l'identite, sinon deux sauts de tete seulement") {
    const auto effective = stitch_generation::effective_sequence(square_project());
    REQUIRE(effective.has_value());

    CHECK(project_io::with_start_point(*effective, {}).commands == effective->commands);

    formats::ExportStart start;
    start.mode = formats::StartMode::Custom;
    start.point = um(-40'000, 25'000);
    const auto moved = project_io::with_start_point(*effective, start);
    REQUIRE(moved.commands.size() == effective->commands.size() + 2);
    CHECK(moved.commands[0].pos == start.point);
    CHECK(moved.commands[0].type == stitch::CommandType::Jump);
    CHECK(moved.commands[1].pos == effective->commands.front().pos);
    CHECK(moved.commands[1].type == stitch::CommandType::Jump);
    // Ordre et positions du motif intacts.
    CHECK(std::equal(effective->commands.begin(), effective->commands.end(),
                     moved.commands.begin() + 2));

    start.mode = formats::StartMode::Center;
    const auto centered = project_io::with_start_point(*effective, start);
    const auto bounds = stitch::compute_stats(*effective).bounds;
    CHECK(centered.commands[0].pos.x.value == (bounds.min.x.value + bounds.max.x.value) / 2);
    CHECK(centered.commands[0].pos.y.value == (bounds.min.y.value + bounds.max.y.value) / 2);
}

TEST_CASE("export_machine_file('dst') : point de depart relu, points inchanges, deterministe") {
    const document::Project project = square_project();
    const auto path = fs::temp_directory_path() / "openstitch_export_start.dst";
    formats::MachineExportOptions options;
    options.start.mode = formats::StartMode::Custom;
    options.start.point = um(-20'000, 15'000);

    REQUIRE(project_io::export_machine_file(project, "dst", path, options).has_value());
    const auto first = formats::read_dst_file(path);
    REQUIRE(project_io::export_machine_file(project, "dst", path, options).has_value());
    const auto second = formats::read_dst_file(path);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->commands == second->commands);

    // Le DST relu est relatif à l'origine machine = le point de départ choisi (au pas de 0,1 mm).
    const auto effective = stitch_generation::effective_sequence(project);
    REQUIRE(effective.has_value());
    REQUIRE(first->commands.size() >= 2);
    // Le saut d'approche (> 12,1 mm) est découpé : on cherche le point d'arrivée parmi les
    // premières commandes relues.
    CHECK(first->commands[0].pos == um(0, 0));
    const Vec2um expectedFirst =
        um(effective->commands.front().pos.x.value - options.start.point.x.value,
           effective->commands.front().pos.y.value - options.start.point.y.value);
    const bool reached =
        std::any_of(first->commands.begin(),
                    first->commands.begin() + std::min<std::size_t>(5, first->commands.size()),
                    [&](const stitch::StitchCommand& c) {
                        return std::abs(c.pos.x.value - expectedFirst.x.value) <= 50 &&
                               std::abs(c.pos.y.value - expectedFirst.y.value) <= 50;
                    });
    CHECK(reached);
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

namespace {
// Deux tatamis de la même couleur qui se chevauchent de 4 mm.
document::Project overlapping_tatamis() {
    document::Project project;
    for (int i = 0; i < 2; ++i) {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        geometry::PathSet set;
        set.outer.closed = true;
        const std::int32_t x0 = i * 6'000;
        set.outer.nodes = {
            {um(x0, 0), geometry::NodeType::Corner, std::nullopt, std::nullopt},
            {um(x0 + 10'000, 0), geometry::NodeType::Corner, std::nullopt, std::nullopt},
            {um(x0 + 10'000, 10'000), geometry::NodeType::Corner, std::nullopt, std::nullopt},
            {um(x0, 10'000), geometry::NodeType::Corner, std::nullopt, std::nullopt},
        };
        vec.paths = {set};
        project.vector_objects.push_back(vec);
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.source_vector = vec.id;
        emb.rgb = {200, 30, 30};
        emb.params = document::TatamiParams{};
        project.embroidery_objects.push_back(emb);
    }
    return project;
}

std::size_t trims_of(const stitch::StitchSequence& s) {
    return static_cast<std::size_t>(
        std::count_if(s.commands.begin(), s.commands.end(),
                      [](const auto& c) { return c.type == stitch::CommandType::Trim; }));
}
} // namespace

TEST_CASE("export_machine_file : optimisation des coupes a la demande, projet intact") {
    const auto project = overlapping_tatamis();
    const auto before = project.finishing;

    // Sans demande : exactement la séquence effective historique.
    const auto plain = stitch_generation::effective_sequence(project);
    REQUIRE(plain.has_value());
    const auto asked = project_io::export_sequence(project);
    REQUIRE(asked.has_value());
    CHECK(asked->sequence.commands == plain->commands);
    CHECK(asked->report.trims_removed == 0);

    // Avec demande : le bilan compte chaque coupe supprimée ; le projet n'est pas modifié.
    project_io::ExportOptimizations opt;
    opt.reduce_trims = true;
    const auto optimized = project_io::export_sequence(project, opt);
    REQUIRE(optimized.has_value());
    CHECK(trims_of(optimized->sequence) + optimized->report.trims_removed == trims_of(*plain));
    CHECK(project.finishing == before);
    CHECK_FALSE(project.finishing.hidden_travel.enabled);

    // Le fichier écrit est celui de la séquence optimisée ; le rapport est renvoyé.
    const auto path = fs::temp_directory_path() / "openstitch_export_trims.dst";
    stitch_generation::TrimReport report;
    REQUIRE(project_io::export_machine_file(project, "dst", path, {}, opt, &report).has_value());
    CHECK(report.trims_removed == optimized->report.trims_removed);
    const auto plainPath = fs::temp_directory_path() / "openstitch_export_plain.dst";
    REQUIRE(project_io::export_machine_file(project, "dst", plainPath).has_value());
    const auto imported = project_io::import_machine_file(path);
    const auto importedPlain = project_io::import_machine_file(plainPath);
    REQUIRE(imported.has_value());
    REQUIRE(importedPlain.has_value());
    CHECK(trims_of(imported->sequence) <= trims_of(importedPlain->sequence));
    fs::remove(path);
    fs::remove(plainPath);
}
