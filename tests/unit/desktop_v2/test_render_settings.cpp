// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "digitize_pipeline.hpp"
#include "preferences.hpp"
#include "preview_service.hpp"
#include "render_settings.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

TEST_CASE("render settings are clamped and presets are distinct", "[desktop_v2][render]") {
    RenderSettings wild;
    wild.pxPerMm = 500.0;
    wild.threadWidthMm = -3.0;
    wild.relief = 9.0;
    wild.shadow = std::nan("");
    const auto c = clamped(wild);
    CHECK(c.pxPerMm == kMaxPxPerMm);
    CHECK(c.threadWidthMm == Catch::Approx(0.1));
    CHECK(c.relief == 1.0);
    CHECK(c.shadow == 0.0);

    for (const char* id : {"draft", "balanced", "high", "photo"}) {
        const auto p = renderPreset(id);
        REQUIRE(p.has_value());
        CHECK(clamped(*p) == *p); // un préréglage est déjà dans les bornes
        CHECK(matchingRenderPreset(*p) == id);
    }
    CHECK_FALSE(renderPreset("nope").has_value());
    CHECK(renderPreset("draft")->pxPerMm < renderPreset("high")->pxPerMm);
    CHECK(renderPreset("draft")->highQuality == false);
    // Le défaut est « équilibré » : le rendu d'origine de la V2 ne change pas.
    CHECK(RenderSettings{} == *renderPreset("balanced"));
    RenderSettings custom = *renderPreset("high");
    custom.sheen = 0.123;
    CHECK(matchingRenderPreset(custom).empty());
}

TEST_CASE("render settings survive the preferences file and bad lines never fail",
          "[desktop_v2][render][prefs]") {
    Preferences p;
    p.render = *renderPreset("photo");
    p.render.fabricRgb = {1, 2, 255};
    const auto back = preferencesFromText(preferencesToText(p));
    CHECK(back.render == p.render);

    const auto partial = preferencesFromText(
        "theme=dark\nrender.px_per_mm=abc\nrender.sheen=0.9\nrender.weave=maybe\nrender.zzz=1\n"
        "render.fabric=12\nrender.quality=fast\n");
    CHECK(partial.theme == "dark");
    CHECK(partial.render.pxPerMm == RenderSettings{}.pxPerMm); // valeur invalide : défaut de la clé
    CHECK(partial.render.sheen == Catch::Approx(0.9));
    CHECK_FALSE(partial.render.fabricWeave);
    CHECK(partial.render.fabricRgb == RenderSettings{}.fabricRgb);
    CHECK_FALSE(partial.render.highQuality);
}

TEST_CASE("preview resolution and quality follow the render settings", "[desktop_v2][render]") {
    DigitizeRequest request;
    request.image = std::filesystem::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 40.0;
    request.settings.max_colors = 3;
    auto digitized = run_digitize(request, TaskContext{});
    REQUIRE(digitized.has_value());
    const auto project = std::make_shared<const document::Project>(std::move(digitized->project));

    const auto draft = render_preview(project, 1, *renderPreset("draft"), TaskContext{});
    const auto high = render_preview(project, 1, *renderPreset("high"), TaskContext{});
    REQUIRE(draft.has_value());
    REQUIRE(high.has_value());
    CHECK(draft->px_per_mm == Catch::Approx(1.5).margin(0.01));
    CHECK(high->px_per_mm == Catch::Approx(6.0).margin(0.01));
    CHECK(high->width > draft->width * 3); // 4× plus fin : l'image est nettement plus grande
    CHECK(high->png != draft->png);
    // L'ancienne signature (résolution seule) reste le rendu « équilibré ».
    const auto legacy = render_preview(project, 1, 3.0, TaskContext{});
    const auto balanced = render_preview(project, 1, RenderSettings{}, TaskContext{});
    REQUIRE(legacy.has_value());
    REQUIRE(balanced.has_value());
    CHECK(legacy->png == balanced->png);
}
