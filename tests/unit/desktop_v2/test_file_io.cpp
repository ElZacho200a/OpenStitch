// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "digitize_pipeline.hpp"
#include "document_session.hpp"
#include "file_io.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/project_io/project_io.hpp"
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

namespace {

// Projet numérisé réel (fixture tentabrode) et sa broderie DST, écrits dans le dossier temporaire.
struct InsertFixtures {
    document::Project project;
    fs::path osp;
    fs::path dst;
};

InsertFixtures makeInsertFixtures() {
    DigitizeRequest request;
    request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 30.0;
    request.settings.max_colors = 3;
    auto digitized = run_digitize(request, TaskContext{});
    REQUIRE(digitized.has_value());
    InsertFixtures out;
    out.project = std::move(digitized->project);
    out.osp = fs::temp_directory_path() / "os_v2_insert_source.osp";
    REQUIRE(project_io::save_project(out.osp, out.project).has_value());
    const auto sequence = stitch_generation::effective_sequence(out.project);
    REQUIRE(sequence.has_value());
    const auto bytes = formats::find_format("dst")->encode(*sequence);
    REQUIRE(bytes.has_value());
    out.dst = fs::temp_directory_path() / "os_v2_insert_source.dst";
    std::ofstream file(out.dst, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes->data()),
               static_cast<std::streamsize>(bytes->size()));
    return out;
}

} // namespace

TEST_CASE("inserting a project adds its objects with fresh ids in one undo step",
          "[desktop_v2][files][insert]") {
    const auto fx = makeInsertFixtures();
    DocumentSession session;
    session.replaceProject(fx.project, true);
    const auto shapes = session.project().vector_objects.size();
    const auto embroideries = session.project().embroidery_objects.size();
    REQUIRE(embroideries > 0);

    const auto preview = previewDesignInsert(session.project(), fx.osp);
    REQUIRE(preview.ok);
    CHECK_FALSE(preview.machine);
    CHECK(preview.shapes == shapes);
    CHECK(preview.embroideries == embroideries);
    CHECK(preview.widthMm > 0.0);
    CHECK(preview.heightMm > 0.0);
    CHECK(session.project().embroidery_objects.size() == embroideries); // l'aperçu ne modifie rien

    const auto depth = session.undoNames().size();
    REQUIRE(insertDesign(session, fx.osp).ok);
    CHECK(session.undoNames().size() == depth + 1);
    CHECK(session.project().vector_objects.size() == 2 * shapes);
    CHECK(session.project().embroidery_objects.size() == 2 * embroideries);
    std::vector<std::uint64_t> ids;
    for (const auto& o : session.project().embroidery_objects) {
        ids.push_back(o.id.value);
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end()); // aucun id réutilisé

    REQUIRE(session.undo());
    CHECK(session.project().embroidery_objects.size() == embroideries);
    CHECK(session.project().vector_objects.size() == shapes);
    fs::remove(fx.osp);
    fs::remove(fx.dst);
}

TEST_CASE("inserting machine embroidery fills an empty project and is refused next to objects",
          "[desktop_v2][files][insert]") {
    const auto fx = makeInsertFixtures();
    DocumentSession session;
    session.replaceProject(document::Project{}, true);
    const auto preview = previewDesignInsert(session.project(), fx.dst);
    REQUIRE(preview.ok);
    CHECK(preview.machine);
    CHECK(preview.stitches > 100);
    CHECK(preview.widthMm > 0.0);
    REQUIRE(insertDesign(session, fx.dst).ok);
    REQUIRE(session.project().imported_design.has_value());
    CHECK(session.project().imported_design->sequence.commands.size() == preview.stitches);
    REQUIRE(session.undo());
    CHECK_FALSE(session.project().imported_design.has_value());

    // Projet avec objets éditables : refus clair, document inchangé.
    session.replaceProject(fx.project, true);
    const auto before = session.project().embroidery_objects.size();
    const auto depth = session.undoNames().size();
    CHECK_FALSE(previewDesignInsert(session.project(), fx.dst).ok);
    const auto refused = insertDesign(session, fx.dst);
    CHECK_FALSE(refused.ok);
    CHECK_FALSE(refused.message.empty());
    CHECK(session.undoNames().size() == depth);
    CHECK(session.project().embroidery_objects.size() == before);
    CHECK_FALSE(session.project().imported_design.has_value());
    fs::remove(fx.osp);
    fs::remove(fx.dst);
}

TEST_CASE("inserting an unreadable or unsupported file is refused and leaves the document intact",
          "[desktop_v2][files][insert]") {
    DocumentSession session;
    session.replaceProject(document::Project{}, true);
    const fs::path junk = fs::temp_directory_path() / "os_v2_insert_junk.osp";
    {
        std::ofstream out(junk, std::ios::binary | std::ios::trunc);
        out << "pas une archive";
    }
    for (const fs::path& file :
         {junk, fs::temp_directory_path() / "os_v2_insert_absent.dst", fs::path("photo.png")}) {
        const auto preview = previewDesignInsert(session.project(), file);
        CHECK_FALSE(preview.ok);
        CHECK_FALSE(preview.message.empty());
        CHECK_FALSE(insertDesign(session, file).ok);
    }
    CHECK_FALSE(session.canUndo());
    fs::remove(junk);
}
