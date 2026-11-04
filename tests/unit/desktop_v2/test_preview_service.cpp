// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "digitize_pipeline.hpp"
#include "preview_service.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

TEST_CASE("preview renders a PNG for a digitized snapshot and tags its revision",
          "[desktop_v2][preview]") {
    DigitizeRequest request;
    request.image = std::filesystem::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 80.0;
    request.settings.max_colors = 4;
    auto digitized = run_digitize(request, TaskContext{});
    REQUIRE(digitized.has_value());
    auto snapshot = std::make_shared<const document::Project>(std::move(digitized->project));
    const auto preview = render_preview(snapshot, 7, 2.0, TaskContext{});
    REQUIRE(preview.has_value());
    CHECK(preview->revision == 7);
    CHECK(preview->width > 100);
    CHECK(preview->height > 100);
    REQUIRE(preview->png.size() > 8);
    CHECK(preview->png[1] == 'P'); // signature PNG : 0x89 'P' 'N' 'G'
    CHECK(preview->png[2] == 'N');
    CHECK(preview->png[3] == 'G');
}

TEST_CASE("a cancelled preview is never delivered", "[desktop_v2][preview]") {
    std::stop_source source;
    source.request_stop();
    TaskContext ctx;
    ctx.stop = source.get_token();
    const auto r = render_preview(std::make_shared<const document::Project>(), 1, 2.0, ctx);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().category == ErrorCategory::Cancelled);
}

TEST_CASE("preview without a project is an internal error, not a crash", "[desktop_v2][preview]") {
    const auto r = render_preview(nullptr, 1, 2.0, TaskContext{});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().category == ErrorCategory::Internal);
}
