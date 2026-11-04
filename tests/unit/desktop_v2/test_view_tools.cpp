// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "document_session.hpp"
#include "edit_batch.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "preview_service.hpp"
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
