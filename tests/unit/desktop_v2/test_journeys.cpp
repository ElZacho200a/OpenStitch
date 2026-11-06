// SPDX-License-Identifier: Apache-2.0
// Parcours de bout en bout de Desktop V2 (sans fenêtre) : chaque test enchaîne les mêmes services
// que l'interface, de l'entrée au fichier machine, et vérifie la CONSÉQUENCE visible (fichier
// écrit, objets conservés, état du guide) plutôt qu'un détail interne.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "diagnostics_service.hpp"
#include "digitize_pipeline.hpp"
#include "document_session.hpp"
#include "export_settings.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "point_edit.hpp"
#include "region_edit.hpp"
#include "shape_batch.hpp"
#include "workflow_guide.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {

fs::path temp(const char* name) {
    return fs::temp_directory_path() / name;
}

document::Project digitizeTentabrode(double heightMm, int colours) {
    DigitizeRequest request;
    request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = heightMm;
    request.settings.max_colors = colours;
    auto out = run_digitize(request, TaskContext{});
    REQUIRE(out.has_value());
    return std::move(out->project);
}

std::uintmax_t exportDst(const std::shared_ptr<const document::Project>& project,
                         const fs::path& file) {
    fs::remove(file);
    const auto stats = export_project_file(project, file, TaskContext{});
    REQUIRE(stats.has_value());
    REQUIRE(fs::exists(file));
    return fs::file_size(file);
}

// Ce que l'interface fournit au guide à un instant donné.
GuideInput guideInput(const DocumentSession& s, std::optional<std::uint64_t> analysed, int errors,
                      std::optional<std::uint64_t> exported) {
    GuideInput in;
    in.documentOpen = true;
    in.hasObjects = !s.project().embroidery_objects.empty();
    in.hasSegmentation = s.project().segmentation.has_value();
    in.analysedEver = analysed.has_value();
    in.analysedCurrent = analysed && *analysed == s.revision();
    in.analysisErrors = in.analysedCurrent ? errors : 0;
    in.exportedEver = exported.has_value();
    in.exportedCurrent = exported && *exported == s.revision();
    return in;
}

} // namespace

TEST_CASE("journey 1: image, segmentation, digitize, check, DST", "[desktop_v2][journey]") {
    DocumentSession session;
    session.replaceProject(digitizeTentabrode(60.0, 5), true);
    REQUIRE(session.project().segmentation.has_value());
    REQUIRE_FALSE(session.project().embroidery_objects.empty());

    const auto diagnostics = run_diagnostics(session.snapshot(), session.revision(), TaskContext{});
    REQUIRE(diagnostics.has_value());
    const auto check = checkExport(session.project(), "dst");
    REQUIRE(check.ok);
    CHECK(check.stitches > 100);

    const auto size = exportDst(session.snapshot(), temp("os_v2_journey1.dst"));
    CHECK(size > 512);
    const auto guide = buildGuide(guideInput(
        session, diagnostics->revision, static_cast<int>(diagnostics->errors), session.revision()));
    CHECK(guide.steps[2].state != StepState::Stale); // contrôle à jour
    fs::remove(temp("os_v2_journey1.dst"));
}

TEST_CASE("journey 2: SVG, objects, points, export", "[desktop_v2][journey]") {
    const fs::path svg = temp("os_v2_journey2.svg");
    {
        std::ofstream out(svg);
        out << R"(<svg xmlns="http://www.w3.org/2000/svg" width="60" height="40" viewBox="0 0 60 40">
  <rect x="5" y="5" width="30" height="20"/>
  <circle cx="45" cy="25" r="10"/>
</svg>)";
    }
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    session.replaceProject(std::move(*created), true);
    const auto r = session.applyBatch(
        R"({"schema":"openstitch.batch/1","operations":[{"op":"import_svg","file":")" +
            svg.generic_string() + R"(","stitch_type":"tatami"}]})",
        "Importer un SVG");
    REQUIRE(r.committed);
    CHECK(session.project().embroidery_objects.size() == 2);
    const auto id = session.project().embroidery_objects.front().id.value;
    const auto points = stitchPoints(session.project(), id);
    REQUIRE(points.has_value());
    CHECK(points->points.size() > 10);
    CHECK(exportDst(session.snapshot(), temp("os_v2_journey2.dst")) > 256);
    fs::remove(svg);
    fs::remove(temp("os_v2_journey2.dst"));
}

TEST_CASE("journey 3: blank project, drawing, embroidery", "[desktop_v2][journey]") {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    session.replaceProject(std::move(*created), true);
    CHECK(session.project().embroidery_objects.empty());
    REQUIRE(session.applyBatch(batchAddRect({0, 0}, 30, 16), "Rectangle").committed);
    REQUIRE(session.applyBatch(batchAddEllipse({25, 0}, 16, 16), "Ellipse").committed);
    CHECK(session.project().embroidery_objects.size() == 2);
    const auto sequence = stitch_generation::effective_sequence(session.project());
    REQUIRE(sequence.has_value());
    CHECK(sequence->commands.size() > 100);
    CHECK(exportDst(session.snapshot(), temp("os_v2_journey3.dst")) > 256);
    fs::remove(temp("os_v2_journey3.dst"));
}

TEST_CASE("journey 4: a saved project reopens, is edited and exported", "[desktop_v2][journey]") {
    const fs::path osp = temp("os_v2_journey4.osp");
    {
        const auto saved = project_io::save_project(osp, digitizeTentabrode(50.0, 4));
        REQUIRE(saved.has_value());
    }
    auto loaded = project_io::load_project(osp);
    REQUIRE(loaded.has_value());
    DocumentSession session;
    session.replaceProject(std::move(*loaded), true);
    const auto before = session.project().embroidery_objects.size();
    REQUIRE(before > 0);
    REQUIRE(session.applyBatch(batchAddRect({0, 0}, 10, 10), "Rectangle").committed);
    CHECK(session.project().embroidery_objects.size() == before + 1);
    CHECK(exportDst(session.snapshot(), temp("os_v2_journey4.dst")) > 512);
    REQUIRE(session.undo());
    CHECK(session.project().embroidery_objects.size() == before);
    fs::remove(osp);
    fs::remove(temp("os_v2_journey4.dst"));
}

TEST_CASE("journey 5: correcting the segmentation after manual edits protects them",
          "[desktop_v2][journey]") {
    DocumentSession session;
    session.replaceProject(digitizeTentabrode(60.0, 5), true);
    // Une forme dessinée à la main (sans région d'origine) et une retouche de point sur un objet
    // issu de la segmentation.
    REQUIRE(session.applyBatch(batchAddRect({0, 0}, 12, 12), "Rectangle").committed);
    const std::uint64_t handDrawn =
        *createdObjectId(session.applyBatch(batchAddEllipse({30, 0}, 10, 10), "Ellipse").report);
    std::uint64_t generated = 0;
    for (const auto& e : session.project().embroidery_objects) {
        const auto v = session.project().findObject(e.source_vector);
        if (v != nullptr && v->source_region) {
            generated = e.id.value;
            break;
        }
    }
    REQUIRE(generated != 0);
    const auto view = stitchPoints(session.project(), generated);
    REQUIRE(view.has_value());
    std::size_t index = 0;
    PointMm from{};
    for (const auto& p : view->points) {
        if (p.movable) {
            index = p.index;
            from = p.pos;
            break;
        }
    }
    REQUIRE(moveStitchPoints(session, generated, {{index, PointMm{from.x + 1.0, from.y}}}).ok);

    const auto edits = countHandEdits(session.project());
    CHECK(edits.retouchedObjects == 1);
    CHECK(edits.handDrawnObjects >= 2);

    // Corriger une région, puis régénérer : rien n'est écrasé en silence.
    const auto regions = allRegionIds(session.project());
    REQUIRE(regions.size() >= 3);
    REQUIRE(recolorRegion(session, regions.front(), {10, 20, 30}).ok);
    auto img = image::load_image(fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png");
    REQUIRE(img.has_value());
    project_ops::DigitizeSettings settings;
    settings.max_colors = 5;
    const auto regen =
        regenerateObjects(session.snapshot(), session.revision(), *img, settings, TaskContext{});
    REQUIRE(regen.has_value());
    CHECK(regen->preserved >= 2);     // les formes dessinées sont conservées
    CHECK(regen->lostRetouches == 1); // la retouche remplacée est comptée, donc annoncée
    const auto depth = session.undoNames().size();
    REQUIRE(applyRegeneration(session, *regen).ok);
    CHECK(session.undoNames().size() == depth + 1);
    CHECK(session.project().findEmbroidery(ObjectId{handDrawn}) != nullptr);
    // Solution réversible : l'annulation rend l'objet retouché avec sa retouche.
    REQUIRE(session.undo());
    const auto* restored = session.project().findEmbroidery(ObjectId{generated});
    REQUIRE(restored != nullptr);
    CHECK_FALSE(restored->overrides.empty());
}

TEST_CASE("journey 6: editing after export makes the check and the export obsolete",
          "[desktop_v2][journey]") {
    DocumentSession session;
    session.replaceProject(digitizeTentabrode(50.0, 4), true);
    const auto analysed = session.revision();
    const auto exported = session.revision();
    auto guide = buildGuide(guideInput(session, analysed, 0, exported));
    CHECK(guide.steps[2].state == StepState::Done);
    REQUIRE(session.applyBatch(batchAddRect({0, 0}, 10, 10), "Rectangle").committed);
    guide = buildGuide(guideInput(session, analysed, 0, exported));
    CHECK(guide.steps[2].state == StepState::Stale);
    CHECK(guide.steps[3].state == StepState::Stale);
    CHECK(guide.next == NextAction::Analyse);
    REQUIRE(
        session.undo()); // le retour en arrière ne redonne pas la validité d'une révision passée
    guide = buildGuide(guideInput(session, analysed, 0, exported));
    CHECK(guide.steps[2].state == StepState::Stale);
}

TEST_CASE("journey 10: a project with many regions and objects stays workable",
          "[desktop_v2][journey]") {
    DocumentSession session;
    session.replaceProject(digitizeTentabrode(150.0, 24), true);
    const auto regions = allRegionIds(session.project());
    REQUIRE(regions.size() >= 6);
    CHECK(session.project().embroidery_objects.size() >=
          100); // « nombreux » : des centaines d'objets
    // Sélection, fusion groupée, réduction de palette et annulation restent des opérations simples.
    const auto depth = session.undoNames().size();
    const std::vector<std::uint64_t> four{regions[0], regions[1], regions[2], regions[3]};
    REQUIRE(mergeRegionSet(session, four).ok);
    REQUIRE(reduceColors(session, 4, 0.0, true).ok);
    CHECK(session.undoNames().size() == depth + 2);
    REQUIRE(session.undo());
    REQUIRE(session.undo());
    CHECK(allRegionIds(session.project()) == regions);
    CHECK(exportDst(session.snapshot(), temp("os_v2_journey10.dst")) > 1024);
    fs::remove(temp("os_v2_journey10.dst"));
}
