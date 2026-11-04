// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <numeric>

#include "document_session.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "production_sheet.hpp"
#include "shape_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

TEST_CASE("an empty document gives an empty production sheet", "[desktop_v2][sheet]") {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    const auto sheet = buildProductionSheet(s.project(), TaskContext{});
    REQUIRE(sheet.has_value());
    CHECK(sheet->stitches == 0);
    CHECK(sheet->blocks.empty());
    CHECK(sheet->hoopWidthMm > 0.0);
}

TEST_CASE("the production sheet counts stitches and lists thread blocks", "[desktop_v2][sheet]") {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    REQUIRE(s.applyBatch(batchAddRect({-10, 0}, 20, 10), "A").committed);
    REQUIRE(s.applyBatch(batchAddEllipse({15, 0}, 12, 12), "B").committed);
    const auto sheet = buildProductionSheet(s.project(), TaskContext{});
    REQUIRE(sheet.has_value());
    CHECK(sheet->objects == 2);
    CHECK(sheet->stitches > 0);
    CHECK(sheet->designWidthMm > 20.0);
    CHECK(sheet->threadMetres > 0.0);
    CHECK(sheet->minutesAt800spm > 0.0);
    REQUIRE_FALSE(sheet->blocks.empty());
    std::size_t total = 0;
    for (const auto& b : sheet->blocks) {
        total += b.stitches;
        CHECK_FALSE(b.objects.empty());
        CHECK(b.lengthMm > 0.0);
    }
    CHECK(total == sheet->stitches);
    CHECK(sheet->fitsHoop);
    // déterministe : même document, même fiche
    const auto again = buildProductionSheet(s.project(), TaskContext{});
    REQUIRE(again.has_value());
    CHECK(again->stitches == sheet->stitches);
    CHECK(again->blocks.size() == sheet->blocks.size());
}
