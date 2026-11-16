// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "document_session.hpp"
#include "edit_batch.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "preview_service.hpp"
#include "render_settings.hpp"
#include "view_tools.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {
DocumentSession withRect() {
    DocumentSession s;
    auto p = project_ops::create_empty_project({});
    REQUIRE(p.has_value());
    s.replaceProject(std::move(*p), true);
    REQUIRE(s.applyBatch(batchAddRect({0, 0}, 30, 20), "R").committed);
    return s;
}
} // namespace

TEST_CASE("the ruler reports distance, components and a counter-clockwise angle",
          "[desktop_v2][view]") {
    const auto m = measure({0, 0}, {3, 4});
    CHECK(m.distanceMm == Catch::Approx(5.0));
    CHECK(m.dxMm == 3.0);
    CHECK(m.dyMm == 4.0);
    CHECK(m.angleDeg == Catch::Approx(53.130102).margin(1e-4));
    CHECK(measure({0, 0}, {-1, 0}).angleDeg == Catch::Approx(180.0));
    CHECK(measure({0, 0}, {0, -2}).angleDeg == Catch::Approx(-90.0));
    CHECK(measure({1, 1}, {1, 1}).distanceMm == 0.0);
    CHECK(measure({1, 1}, {1, 1}).angleDeg == 0.0);
}

TEST_CASE("grid lines fall on multiples of the spacing about the model origin",
          "[desktop_v2][view]") {
    auto s = withRect();
    auto preview = render_preview(s.snapshot(), 1, 4.0, TaskContext{});
    REQUIRE(preview.has_value());
    const auto grid = gridLines(*preview, 10.0);
    REQUIRE_FALSE(grid.empty());
    // Cadre 100 x 100 mm centré : traits à -40..40 (bords exclus par la fenêtre un peu plus large).
    REQUIRE(grid.verticalPx.size() >= 9);
    for (const double px : grid.verticalPx) {
        const double mm = preview->pxToMmX(px);
        CHECK(std::fmod(std::abs(mm) + 1e-6, 10.0) < 1e-3 + 2e-6);
    }
    // Le trait x = 0 mm est le centre du modèle.
    bool zero = false;
    for (const double px : grid.verticalPx) {
        zero = zero || std::abs(preview->pxToMmX(px)) < 1e-6;
    }
    CHECK(zero);
    CHECK(gridLines(*preview, 0.0).empty());
    CHECK(gridLines(*preview, -3.0).empty());
    CHECK(gridLines(*preview, 0.05).empty()); // trop dense : on ne trace rien
}

TEST_CASE("simulation renders a prefix of the sewing order and keeps the frame",
          "[desktop_v2][view][preview]") {
    auto s = withRect();
    const auto full = render_preview(s.snapshot(), 1, 3.0, TaskContext{});
    REQUIRE(full.has_value());
    REQUIRE(full->command_count > 20);
    const auto half = render_preview(s.snapshot(), 1, 3.0, TaskContext{}, full->command_count / 2);
    const auto none = render_preview(s.snapshot(), 1, 3.0, TaskContext{}, 0);
    REQUIRE(half.has_value());
    REQUIRE(none.has_value());
    // Même cadre : la vue ne bouge pas quand on fait glisser le curseur.
    CHECK(half->width == full->width);
    CHECK(half->origin_x_mm == full->origin_x_mm);
    CHECK(half->command_count == full->command_count);
    // Les images diffèrent : moins de fil posé.
    CHECK(half->png != full->png);
    CHECK(none->png != half->png);
    // Au-delà de la fin : identique à l'aperçu complet.
    const auto beyond =
        render_preview(s.snapshot(), 1, 3.0, TaskContext{}, full->command_count + 5);
    REQUIRE(beyond.has_value());
    CHECK(beyond->png == full->png);
}

TEST_CASE("the preview PNG export writes a valid image and refuses bad resolutions",
          "[desktop_v2][view]") {
    auto s = withRect();
    const fs::path out = fs::temp_directory_path() / "openstitch_v2_view.png";
    fs::remove(out);
    REQUIRE(export_preview_png(s.snapshot(), out, 4.0, TaskContext{}).has_value());
    REQUIRE(fs::exists(out));
    std::ifstream in(out, std::ios::binary);
    unsigned char sig[4] = {};
    in.read(reinterpret_cast<char*>(sig), 4);
    in.close();
    CHECK(sig[1] == 'P');
    CHECK(sig[2] == 'N');
    CHECK(sig[3] == 'G');
    fs::remove(out);
    CHECK_FALSE(export_preview_png(s.snapshot(), out, 0.2, TaskContext{}).has_value());
    CHECK_FALSE(export_preview_png(s.snapshot(), out, 50.0, TaskContext{}).has_value());
    CHECK_FALSE(fs::exists(out));
    std::stop_source stop;
    stop.request_stop();
    TaskContext cancelled;
    cancelled.stop = stop.get_token();
    CHECK(export_preview_png(s.snapshot(), out, 4.0, cancelled).error().category ==
          ErrorCategory::Cancelled);
}

namespace {
// Largeur et hauteur lues dans l'en-tête IHDR d'un PNG (octets 16 à 23, gros-boutiste).
std::pair<int, int> pngSize(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    unsigned char header[24] = {};
    in.read(reinterpret_cast<char*>(header), 24);
    const auto be = [&](int at) {
        return (header[at] << 24) | (header[at + 1] << 16) | (header[at + 2] << 8) | header[at + 3];
    };
    return {be(16), be(20)};
}
} // namespace

TEST_CASE("the image export plan announces exactly the size of the written PNG",
          "[desktop_v2][view][export_plan]") {
    auto s = withRect();
    for (const double pxPerMm : {1.0, 4.0, 7.5}) {
        const auto plan = plan_image_export(s.snapshot(), pxPerMm, TaskContext{});
        REQUIRE(plan.has_value());
        CHECK_FALSE(plan->reduced);
        CHECK(plan->pxPerMm == Catch::Approx(pxPerMm));
        const fs::path out = fs::temp_directory_path() / "openstitch_v2_plan.png";
        REQUIRE(export_preview_png(s.snapshot(), out, pxPerMm, TaskContext{}).has_value());
        const auto [w, h] = pngSize(out);
        CHECK(w == plan->width);
        CHECK(h == plan->height);
        // Échelle 1:1 : pixels = millimètres de la fenêtre × résolution.
        CHECK(plan->widthMm * pxPerMm == Catch::Approx(w).margin(1.0));
        CHECK(plan->heightMm * pxPerMm == Catch::Approx(h).margin(1.0));
        fs::remove(out);
    }
    // Hors de la plage du rendu : refus, jamais une résolution corrigée en silence.
    CHECK_FALSE(plan_image_export(s.snapshot(), 0.5, TaskContext{}).has_value());
    CHECK_FALSE(plan_image_export(s.snapshot(), kMaxPxPerMm + 1, TaskContext{}).has_value());
    CHECK_FALSE(export_preview_png(s.snapshot(), fs::temp_directory_path() / "x.png",
                                   kMaxPxPerMm + 1, TaskContext{})
                    .has_value());
}

TEST_CASE("the placement template plan matches the pages and paper of the written PDF",
          "[desktop_v2][view][export_plan]") {
    auto s = withRect();
    CHECK_FALSE(placement_template_options("A5", false, "x").has_value());
    struct Case {
        const char* paper;
        bool landscape;
        double widthPt;
        double heightPt;
    };
    for (const Case c : {Case{"A4", false, 595.28, 841.89}, Case{"A3", true, 1190.55, 841.89},
                         Case{"Letter", false, 612.0, 792.0}}) {
        const auto options = placement_template_options(c.paper, c.landscape, "Essai");
        REQUIRE(options.has_value());
        const auto plan = plan_placement_template(*s.snapshot(), *options, TaskContext{});
        REQUIRE(plan.has_value());
        CHECK(plan->pages == plan->columns * plan->rows);
        CHECK(plan->pages >= 1);
        CHECK(plan->designWidthMm > 0.0);
        CHECK(plan->pageWidthMm * 72.0 / 25.4 == Catch::Approx(c.widthPt).margin(0.01));
        CHECK(plan->pageHeightMm * 72.0 / 25.4 == Catch::Approx(c.heightPt).margin(0.01));

        const auto sequence = stitch_generation::effective_sequence(*s.snapshot());
        REQUIRE(sequence.has_value());
        const auto pdf = stitch_analysis::placement_template_to_pdf(
            stitch_analysis::make_placement_template(*sequence, *options));
        std::size_t pages = 0;
        for (auto at = pdf.find("/Type /Page "); at != std::string::npos;
             at = pdf.find("/Type /Page ", at + 1)) {
            ++pages;
        }
        CHECK(pages == plan->pages);
        char mediaBox[64];
        std::snprintf(mediaBox, sizeof mediaBox, "/MediaBox [0 0 %.2f %.2f]", c.widthPt,
                      c.heightPt);
        CHECK(pdf.find(mediaBox) != std::string::npos);
    }
    // Sans point : refus clair.
    document::Project empty;
    const auto options = placement_template_options("A4", false, "");
    CHECK_FALSE(plan_placement_template(empty, *options, TaskContext{}).has_value());
}
