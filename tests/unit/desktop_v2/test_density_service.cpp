// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "density_service.hpp"
#include "document_session.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "shape_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
DocumentSession oneRect() {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    REQUIRE(s.applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle").committed);
    return s;
}
} // namespace

TEST_CASE("density colours are transparent when sparse and red when over the threshold",
          "[desktop_v2][density]") {
    CHECK(densityColour(0.0, 4.0)[3] == 0);
    CHECK(densityColour(0.5, 4.0)[3] == 0); // sous le quart du seuil
    const auto mid = densityColour(2.0, 4.0);
    CHECK(mid[3] > 0);
    CHECK(mid[3] < densityColour(4.0, 4.0)[3]);
    const auto hot = densityColour(9.0, 4.0);
    CHECK(hot[0] > 200);
    CHECK(hot[1] < 60);
    CHECK(densityColour(5.0, 0.0)[3] == 0); // seuil nul : aucune zone signalée
}

TEST_CASE("an empty document has no density map", "[desktop_v2][density]") {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    TaskContext task;
    CHECK_FALSE(run_density(s.snapshot(), s.revision(), task).has_value());
}

TEST_CASE("the density overlay covers the motif and carries the analysed revision",
          "[desktop_v2][density]") {
    auto s = oneRect();
    TaskContext task;
    const auto r = run_density(s.snapshot(), s.revision(), task);
    REQUIRE(r.has_value());
    CHECK(r->revision == s.revision());
    CHECK(r->columns > 0);
    CHECK(r->rows > 0);
    CHECK(r->rgba.size() == static_cast<std::size_t>(r->columns) * r->rows * 4);
    // Le rectangle de 20 x 10 mm est couvert (à une case près de chaque côté).
    CHECK(r->widthMm >= 20.0);
    CHECK(r->heightMm >= 10.0);
    CHECK(r->widthMm <= 23.0);
    CHECK(r->maxPenetrationsPerMm2 > 0.0);
    // Déterminisme : même entrée, même image.
    const auto again = run_density(s.snapshot(), s.revision(), task);
    REQUIRE(again.has_value());
    CHECK(again->rgba == r->rgba);
}
