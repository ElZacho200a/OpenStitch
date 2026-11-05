// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "document_session.hpp"
#include "fabric_risk.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "shape_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
DocumentSession withRect(double widthMm, double heightMm) {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    REQUIRE(s.applyBatch(batchAddRect({0, 0}, widthMm, heightMm), "Rectangle").committed);
    return s;
}
} // namespace

TEST_CASE("risk levels follow the critical threshold", "[desktop_v2][fabric]") {
    FabricRisk r;
    r.maxWrinkleIndex = 0.2;
    CHECK(r.level() == "faible");
    r.maxWrinkleIndex = 0.7;
    CHECK(r.level() == "à surveiller");
    r.maxWrinkleIndex = 1.3;
    CHECK(r.level() == "élevé");
}

TEST_CASE("the verdict always says the model is not calibrated until it is",
          "[desktop_v2][fabric]") {
    FabricRisk r;
    r.converged = true;
    CHECK(r.verdict().find("NON calibrée") != std::string::npos);
    r.calibrated = true;
    CHECK(r.verdict().find("NON calibrée") == std::string::npos);
    r.converged = false;
    CHECK(r.verdict().find("convergé") != std::string::npos);
}

TEST_CASE("an empty document has no fabric analysis", "[desktop_v2][fabric]") {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    TaskContext task;
    CHECK_FALSE(runFabricRisk(s.snapshot(), s.revision(), task).has_value());
}

TEST_CASE("a dense fill yields a map, displacements and an honest verdict",
          "[desktop_v2][fabric]") {
    auto s = withRect(40, 30);
    TaskContext task;
    const auto risk = runFabricRisk(s.snapshot(), s.revision(), task);
    REQUIRE(risk.has_value());
    CHECK_FALSE(risk->empty());
    CHECK(risk->rgba.size() == static_cast<std::size_t>(risk->columns) * risk->rows * 4);
    CHECK(risk->widthMm >= 40.0);
    CHECK(risk->heightMm >= 30.0);
    CHECK(risk->revision == s.revision());
    CHECK_FALSE(risk->calibrated); // jamais présenté comme mesuré
    CHECK(risk->maxDisplacementMm > 0.0);
    CHECK_FALSE(risk->arrows.empty());
    CHECK(risk->arrows.size() <= 13 * 13);
    CHECK(risk->verdict().find("NON calibrée") != std::string::npos);
    CHECK(s.revision() == risk->revision); // lecture seule
}

TEST_CASE("the fabric analysis is deterministic", "[desktop_v2][fabric]") {
    auto s = withRect(30, 30);
    TaskContext task;
    const auto a = runFabricRisk(s.snapshot(), s.revision(), task);
    const auto b = runFabricRisk(s.snapshot(), s.revision(), task);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->rgba == b->rgba);
    CHECK(a->maxWrinkleIndex == b->maxWrinkleIndex);
    CHECK(a->arrows.size() == b->arrows.size());
}
