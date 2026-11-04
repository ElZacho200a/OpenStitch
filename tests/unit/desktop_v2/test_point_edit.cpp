// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "document_session.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "point_edit.hpp"
#include "shape_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

DocumentSession sessionWithRect(std::uint64_t& id) {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    const auto r = s.applyBatch(batchAddRect({0, 0}, 20, 12), "Rectangle");
    REQUIRE(r.committed);
    id = *createdObjectId(r.report);
    return s;
}

std::size_t firstMovable(const PointView& v) {
    const auto it = std::find_if(v.points.begin(), v.points.end(),
                                 [](const EditablePoint& p) { return p.movable; });
    REQUIRE(it != v.points.end());
    return it->index;
}

} // namespace

TEST_CASE("an object exposes its top stitch points", "[desktop_v2][points]") {
    std::uint64_t id = 0;
    auto s = sessionWithRect(id);
    const auto v = stitchPoints(s.project(), id);
    REQUIRE(v.has_value());
    CHECK(v->state == PointEditState::Clean);
    CHECK(v->points.size() > 10);
    CHECK(v->editedCount == 0);
    CHECK_FALSE(stitchPoints(s.project(), 999999).has_value());
}

TEST_CASE("moving points is one undo step and survives regeneration", "[desktop_v2][points]") {
    std::uint64_t id = 0;
    auto s = sessionWithRect(id);
    const auto v = stitchPoints(s.project(), id);
    REQUIRE(v.has_value());
    const auto idx = firstMovable(*v);
    const auto from = v->points[0].pos;
    const auto depth = s.undoNames().size();
    const auto r = moveStitchPoints(s, id, {{idx, PointMm{from.x + 1.5, from.y + 0.5}}});
    REQUIRE(r.ok);
    CHECK(s.undoNames().size() == depth + 1);
    const auto after = stitchPoints(s.project(), id);
    REQUIRE(after.has_value());
    CHECK(after->state == PointEditState::ManuallyEdited);
    CHECK(after->editedCount == 1);
    REQUIRE(s.undo());
    const auto back = stitchPoints(s.project(), id);
    REQUIRE(back.has_value());
    CHECK(back->state == PointEditState::Clean);
}

TEST_CASE("delete restore insert and discard edit points", "[desktop_v2][points]") {
    std::uint64_t id = 0;
    auto s = sessionWithRect(id);
    const auto v = stitchPoints(s.project(), id);
    REQUIRE(v.has_value());
    const auto idx = firstMovable(*v);
    REQUIRE(deleteStitchPoints(s, id, {idx}).ok);
    CHECK(stitchPoints(s.project(), id)->editedCount == 1);
    REQUIRE(restoreStitchPoints(s, id, {idx}).ok);
    REQUIRE(insertStitchPoint(s, id, idx, PointMm{1.0, 1.0}).ok);
    CHECK(stitchPoints(s.project(), id)->state == PointEditState::ManuallyEdited);
    REQUIRE(discardStitchEdits(s, id).ok);
    CHECK(stitchPoints(s.project(), id)->state == PointEditState::Clean);
    CHECK_FALSE(discardStitchEdits(s, id).ok); // plus rien a effacer
}

TEST_CASE("point edits refuse bad requests without stacking anything", "[desktop_v2][points]") {
    std::uint64_t id = 0;
    auto s = sessionWithRect(id);
    const auto depth = s.undoNames().size();
    CHECK_FALSE(moveStitchPoints(s, id, {}).ok);
    CHECK_FALSE(moveStitchPoints(s, id, {{999999, PointMm{0, 0}}}).ok);
    CHECK_FALSE(moveStitchPoints(s, 424242, {{0, PointMm{0, 0}}}).ok);
    const auto v = stitchPoints(s.project(), id);
    const auto idx = firstMovable(*v);
    CHECK_FALSE(moveStitchPoints(s, id, {{idx, PointMm{5000, 0}}}).ok);
    CHECK_FALSE(moveStitchPoints(s, id, {{idx, PointMm{std::nan(""), 0}}}).ok);
    CHECK_FALSE(deleteStitchPoints(s, id, {}).ok);
    CHECK_FALSE(deleteStitchPoints(s, id, {999999}).ok);
    CHECK_FALSE(insertStitchPoint(s, id, 999999, PointMm{0, 0}).ok);
    CHECK_FALSE(restoreStitchPoints(s, id, {999999}).ok);
    CHECK(s.undoNames().size() == depth);
}
