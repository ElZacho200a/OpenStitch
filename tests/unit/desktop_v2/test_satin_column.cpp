// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "document_session.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "preview_service.hpp"
#include "satin_column.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
DocumentSession newSession() {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    return s;
}

std::vector<std::pair<PointMm, PointMm>> ladder() {
    return {{{-10, 0}, {-10, 4}}, {{0, 1}, {0, 5}}, {{10, 0}, {10, 4}}};
}
} // namespace

TEST_CASE("a satin column is created from rung pairs in one undo step", "[desktop_v2][satin]") {
    auto s = newSession();
    const auto depth = s.undoNames().size();
    const auto r = createSatinColumn(s, ladder());
    REQUIRE(r.ok);
    CHECK_FALSE(r.railsCross);
    CHECK(s.undoNames().size() == depth + 1);
    CHECK(s.project().embroidery_objects.size() == 1);
    CHECK(railNodeCount(s.project(), r.objectId, RailSide::A) == 3);
    CHECK(railNodeCount(s.project(), r.objectId, RailSide::B) == 3);
    const auto preview = render_preview(s.snapshot(), s.revision(), 4.0, TaskContext{});
    REQUIRE(preview.has_value());
    CHECK(preview->hits->boundsOf(r.objectId).has_value());
    REQUIRE(s.undo());
    CHECK(s.project().embroidery_objects.empty());
}

TEST_CASE("crossing rails are reported but still created", "[desktop_v2][satin]") {
    auto s = newSession();
    const auto r = createSatinColumn(s, {{{0, 0}, {10, 0}}, {{10, 5}, {0, 5}}});
    REQUIRE(r.ok);
    CHECK(r.railsCross);
}

TEST_CASE("bad satin requests are refused without stacking anything", "[desktop_v2][satin]") {
    auto s = newSession();
    const auto depth = s.undoNames().size();
    CHECK_FALSE(createSatinColumn(s, {}).ok);
    CHECK_FALSE(createSatinColumn(s, {{{0, 0}, {0, 4}}}).ok);
    CHECK_FALSE(createSatinColumn(s, {{{0, 0}, {0, 0}}, {{5, 0}, {5, 4}}}).ok);
    CHECK_FALSE(createSatinColumn(s, {{{0, 0}, {0, 4}}, {{5000, 0}, {5, 4}}}).ok);
    CHECK(s.undoNames().size() == depth);
    CHECK_FALSE(moveSatinRailNode(s, 12345, RailSide::A, 0, {0, 0}).ok);
    CHECK_FALSE(insertSatinRailNode(s, 12345, RailSide::A, 0, 0.5).ok);
}

TEST_CASE("rail nodes can be moved and inserted", "[desktop_v2][satin]") {
    auto s = newSession();
    const auto r = createSatinColumn(s, ladder());
    REQUIRE(r.ok);
    const auto depth = s.undoNames().size();
    REQUIRE(moveSatinRailNode(s, r.objectId, RailSide::A, 1, {0, -2}).ok);
    CHECK(s.undoNames().size() == depth + 1);
    CHECK_FALSE(moveSatinRailNode(s, r.objectId, RailSide::A, 9, {0, 0}).ok);
    CHECK_FALSE(moveSatinRailNode(s, r.objectId, RailSide::A, 1, {5000, 0}).ok);
    REQUIRE(insertSatinRailNode(s, r.objectId, RailSide::B, 0, 0.5).ok);
    CHECK(railNodeCount(s.project(), r.objectId, RailSide::B) == 4);
    CHECK_FALSE(insertSatinRailNode(s, r.objectId, RailSide::B, 0, 1.0).ok);
    CHECK_FALSE(insertSatinRailNode(s, r.objectId, RailSide::B, 9, 0.5).ok);
    REQUIRE(s.undo());
    CHECK(railNodeCount(s.project(), r.objectId, RailSide::B) == 3);
}
