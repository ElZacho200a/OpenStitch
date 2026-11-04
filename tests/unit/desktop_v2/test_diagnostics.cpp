// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "diagnostics_service.hpp"
#include "digitize_pipeline.hpp"
#include "openstitch/project_ops/factory.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

TEST_CASE("diagnostics of a digitized document are consistent and tagged with the revision",
          "[desktop_v2][diagnostics]") {
    DigitizeRequest request;
    request.image = std::filesystem::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 80.0;
    request.settings.max_colors = 4;
    auto digitized = run_digitize(request, TaskContext{});
    REQUIRE(digitized.has_value());
    const auto snapshot = std::make_shared<const document::Project>(std::move(digitized->project));
    const auto result = run_diagnostics(snapshot, 42, TaskContext{});
    REQUIRE(result.has_value());
    CHECK(result->revision == 42);
    CHECK(result->errors + result->warnings + result->infos == result->findings.size());
    CHECK(result->stitches > 1000);
    CHECK(result->motifWidthMm > 10.0);
    for (const auto& f : result->findings) {
        CHECK((f.severity == "error" || f.severity == "warning" || f.severity == "info"));
        CHECK_FALSE(f.code.empty());
    }
}

TEST_CASE("diagnostics refuse an empty document and honour cancellation",
          "[desktop_v2][diagnostics]") {
    auto empty = project_ops::create_empty_project({});
    REQUIRE(empty.has_value());
    const auto r = run_diagnostics(std::make_shared<const document::Project>(std::move(*empty)), 1,
                                   TaskContext{});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().category == ErrorCategory::OperationImpossible);

    std::stop_source source;
    source.request_stop();
    TaskContext ctx;
    ctx.stop = source.get_token();
    DigitizeRequest request;
    request.image = std::filesystem::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
    request.target_height_mm = 40.0;
    request.settings.max_colors = 3;
    auto digitized = run_digitize(request, TaskContext{});
    REQUIRE(digitized.has_value());
    const auto cancelled = run_diagnostics(
        std::make_shared<const document::Project>(std::move(digitized->project)), 1, ctx);
    REQUIRE_FALSE(cancelled.has_value());
    CHECK(cancelled.error().category == ErrorCategory::Cancelled);
    CHECK_FALSE(run_diagnostics(nullptr, 1, TaskContext{}).has_value());
}
