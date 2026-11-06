// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "digitize_pipeline.hpp"
#include "document_session.hpp"
#include "file_io.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

TEST_CASE("machine formats come from the registry and files are recognised by extension",
          "[desktop_v2][files]") {
    const auto filters = readableMachineFormats();
    REQUIRE(filters.size() >= 2);
    const auto& all = filters.front();
    CHECK(std::find(all.patterns.begin(), all.patterns.end(), "*.dst") != all.patterns.end());
    CHECK(isMachineDesignFile("motif.DST"));
    CHECK(isMachineDesignFile(fs::path("dossier") / "motif.pes"));
    CHECK_FALSE(isMachineDesignFile("photo.png"));
    CHECK_FALSE(isMachineDesignFile("plan.dxf"));
    CHECK_FALSE(isMachineDesignFile("sans_extension"));
}

TEST_CASE("a machine embroidery opens as a new project with its stitches", "[desktop_v2][files]") {
    DigitizeRequest request;
    request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 40.0;
    request.settings.max_colors = 3;
    auto digitized = run_digitize(request, TaskContext{});
    REQUIRE(digitized.has_value());
    const auto sequence = stitch_generation::effective_sequence(digitized->project);
    REQUIRE(sequence.has_value());
    const auto* dst = formats::find_format("dst");
    REQUIRE(dst != nullptr);
    REQUIRE(dst->encode != nullptr);
    const auto bytes = dst->encode(*sequence);
    REQUIRE(bytes.has_value());

    const fs::path file = fs::temp_directory_path() / "os_v2_import_machine.dst";
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes->data()),
                  static_cast<std::streamsize>(bytes->size()));
    }
    const auto loaded = loadMachineProject(file);
    REQUIRE(loaded.has_value());
    CHECK(loaded->imported_design.has_value());
    CHECK(loaded->embroidery_objects.empty()); // un design machine n'a pas d'objets éditables
    const auto imported = stitch_generation::effective_sequence(*loaded);
    REQUIRE(imported.has_value());
    CHECK(imported->commands.size() > 100);

    // Fichier absent ou illisible : refus clair, jamais un projet vide.
    CHECK_FALSE(loadMachineProject(fs::temp_directory_path() / "os_v2_absent.dst").has_value());
    const fs::path junk = fs::temp_directory_path() / "os_v2_junk.dst";
    {
        std::ofstream out(junk, std::ios::binary | std::ios::trunc);
        out << "ceci n'est pas une broderie";
    }
    CHECK_FALSE(loadMachineProject(junk).has_value());
    fs::remove(file);
    fs::remove(junk);
}

TEST_CASE("DXF export then import round-trips the vector outlines in one undo step",
          "[desktop_v2][files]") {
    DigitizeRequest request;
    request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 40.0;
    request.settings.max_colors = 3;
    auto digitized = run_digitize(request, TaskContext{});
    REQUIRE(digitized.has_value());

    const fs::path file = fs::temp_directory_path() / "os_v2_roundtrip.dxf";
    std::size_t written = 0;
    REQUIRE(exportVectorsToDxf(digitized->project, file, &written).ok);
    CHECK(written > 0);

    DocumentSession session;
    session.replaceProject(document::Project{}, true);
    const auto depth = session.undoNames().size();
    const auto result = session.applyBatch(
        R"({"schema":"openstitch.batch/1","operations":[{"op":"import_dxf","file":")" +
            file.generic_string() + R"("}]})",
        "Importer un DXF");
    REQUIRE(result.committed);
    CHECK(session.undoNames().size() == depth + 1);
    CHECK(session.project().vector_objects.size() == written);
    CHECK(session.project().embroidery_objects.empty()); // à numériser ensuite
    REQUIRE(session.undo());
    CHECK(session.project().vector_objects.empty());

    // Rien à exporter : refus clair. Fichier absent : l'import est refusé, document intact.
    CHECK_FALSE(exportVectorsToDxf(document::Project{}, file).ok);
    const auto absent = session.applyBatch(
        R"({"schema":"openstitch.batch/1","operations":[{"op":"import_dxf","file":"/absent/x.dxf"}]})",
        "Importer un DXF");
    CHECK_FALSE(absent.committed);
    fs::remove(file);
}
