// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "action_catalog.hpp"
#include "digitize_pipeline.hpp"
#include "document_session.hpp"
#include "edit_batch.hpp"
#include "inspector_model.hpp"
#include "object_labels.hpp"
#include "openstitch/project_ops/factory.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {

DocumentSession blank() {
    DocumentSession s;
    auto p = project_ops::create_empty_project({});
    REQUIRE(p.has_value());
    s.replaceProject(std::move(*p), true);
    return s;
}

// Applique une action par son identifiant, avec des valeurs, comme le fait l'interface.
DocumentSession::BatchResult run(DocumentSession& s, const std::string& id, Values values,
                                 std::vector<std::uint64_t> ids = {}) {
    const ActionDef* def = findAction(id);
    REQUIRE(def != nullptr);
    ActionContext ctx;
    ctx.ids = std::move(ids);
    ctx.project = &s.project();
    std::string error;
    const std::string batch = buildAction(*def, std::move(values), ctx, error);
    INFO(id << " : " << error);
    REQUIRE_FALSE(batch.empty());
    return s.applyBatch(batch, def->label);
}

std::string fontFile() {
    return (fs::path(OPENSTITCH_FONTS_DIR) / "VeraBd.ttf").string();
}

} // namespace

TEST_CASE("action ids are unique and every field has a valid default", "[desktop_v2][actions]") {
    std::vector<std::string> seen;
    for (const auto& a : actionCatalog()) {
        CHECK(std::ranges::find(seen, a.id) == seen.end());
        seen.push_back(a.id);
        CHECK_FALSE(a.label.empty());
        CHECK_FALSE(a.group.empty());
        CHECK(static_cast<bool>(a.build));
        for (const auto& f : a.fields) {
            CHECK_FALSE(f.key.empty());
            if (f.kind == FieldKind::Choice) {
                REQUIRE_FALSE(f.choices.empty());
                CHECK(f.def == f.choices.front().value);
            }
            if (f.kind == FieldKind::Number || f.kind == FieldKind::Integer) {
                const double d = std::stod(f.def);
                CHECK(d >= f.min);
                CHECK(d <= f.max);
            }
        }
    }
    CHECK(actionCatalog().size() >= 9);
}

TEST_CASE("validation refuses out-of-range, unknown, missing and unselected input",
          "[desktop_v2][actions]") {
    std::string error;
    ActionContext none;
    const auto* canvas = findAction("canvas.set");
    REQUIRE(canvas != nullptr);
    CHECK(buildAction(*canvas, {{"width_mm", "5000"}}, none, error).empty());
    CHECK_FALSE(error.empty());
    CHECK(buildAction(*canvas, {{"width_mm", "abc"}}, none, error).empty());
    CHECK(buildAction(*canvas, {{"shape", "triangle"}}, none, error).empty());
    CHECK(buildAction(*canvas, {{"width_mm", "50"}, {"height_mm", "40"}, {"margin_mm", "30"}}, none,
                      error)
              .empty()); // marge >= moitié du petit côté
    CHECK_FALSE(error.empty());
    const auto* satin = findAction("satin.auto");
    REQUIRE(satin != nullptr);
    CHECK(buildAction(*satin, {}, none, error).empty()); // rien de sélectionné
    CHECK(error.find("Sélectionnez") != std::string::npos);
    const auto* text = findAction("text.add");
    CHECK(buildAction(*text, {{"text", ""}, {"font_file", fontFile()}}, none, error).empty());
    CHECK(buildAction(*text, {{"font_file", "/no/such/font.ttf"}}, none, error).empty());
    const auto* denoise = findAction("image.denoise");
    CHECK(buildAction(*denoise, {{"strength", "1.5"}}, none, error).empty()); // entier attendu
}

TEST_CASE("canvas, machine and finishing actions change the document in one undo step each",
          "[desktop_v2][actions]") {
    auto s = blank();
    REQUIRE(run(s, "canvas.set", {{"width_mm", "150"}, {"height_mm", "120"}, {"shape", "ellipse"}})
                .committed);
    CHECK(s.project().canvas.width.value == 150'000);
    CHECK(s.project().canvas.height.value == 120'000);
    CHECK(s.project().canvas.shape == document::HoopShape::Ellipse);

    REQUIRE(run(s, "machine.set", {}).committed);
    REQUIRE(run(s, "finishing.set",
                {{"trim_threshold_mm", "5"}, {"lock", "triangle"}, {"hidden_travel", "1"}})
                .committed);
    CHECK(s.undoNames().size() == 3);
    REQUIRE(s.undo());
    REQUIRE(s.undo());
    REQUIRE(s.undo());
    CHECK(s.project().canvas.width.value == 150'000 - 50'000); // retour au cadre d'origine (100 mm)
}

TEST_CASE("add text creates stitched objects from a font", "[desktop_v2][actions]") {
    auto s = blank();
    const auto r =
        run(s, "text.add", {{"text", "Hi"}, {"cap_height_mm", "20"}, {"font_file", fontFile()}});
    REQUIRE(r.committed);
    CHECK_FALSE(s.project().embroidery_objects.empty());
}

TEST_CASE("an SVG import creates objects; a missing file is refused before any batch is built",
          "[desktop_v2][actions]") {
    const fs::path svg = fs::temp_directory_path() / "openstitch_v2_import.svg";
    {
        std::ofstream out(svg);
        out << R"(<svg xmlns="http://www.w3.org/2000/svg" width="40mm" height="30mm" viewBox="0 0 40 30">)"
            << R"(<rect x="5" y="5" width="30" height="20" fill="#cc3333"/></svg>)";
    }
    auto s = blank();
    REQUIRE(
        run(s, "file.import_svg", {{"file", svg.string()}, {"stitch_type", "tatami"}}).committed);
    CHECK_FALSE(s.project().embroidery_objects.empty());
    fs::remove(svg);
    std::string error;
    CHECK(buildAction(*findAction("file.import_svg"), {{"file", svg.string()}}, {}, error).empty());
    CHECK(error.find("introuvable") != std::string::npos);
}

TEST_CASE("auto satin converts the selected objects and guides apply to a directional fill",
          "[desktop_v2][actions]") {
    auto s = blank();
    const auto id = *createdObjectId(s.applyBatch(batchAddRect({0, 0}, 40, 6), "Bande").report);
    REQUIRE(run(s, "satin.auto", {{"spacing_mm", "0.35"}}, {id}).committed);
    const auto type = std::string(stitchTypeId(s.project().embroidery_objects.front()));
    CHECK((type == "auto_satin" || type == "satin"));

    auto g = blank();
    const auto gid = *createdObjectId(g.applyBatch(batchAddRect({0, 0}, 30, 20), "Zone").report);
    REQUIRE(g.applyBatch(batchSetStitchType({gid}, "directional"), "Type").committed);
    CHECK(run(g, "fill.guides", {}, {gid}).committed);
}

TEST_CASE("image actions run on a digitized document", "[desktop_v2][actions]") {
    DigitizeRequest request;
    request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 60.0;
    request.settings.max_colors = 4;
    auto digitized = run_digitize(request, TaskContext{});
    REQUIRE(digitized.has_value());
    DocumentSession s;
    s.replaceProject(std::move(digitized->project), true);
    CHECK(run(s, "image.denoise", {{"method", "median"}, {"strength", "1"}}).committed);
    CHECK(run(s, "image.segment", {{"max_colors", "6"}}).committed);
}
