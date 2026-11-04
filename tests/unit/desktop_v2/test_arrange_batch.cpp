// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "arrange_batch.hpp"
#include "document_session.hpp"
#include "openstitch/commands/placement_ops.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "shape_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

DocumentSession threeRects() {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    REQUIRE(s.applyBatch(batchAddRect({-30, 0}, 10, 10), "A").committed);
    REQUIRE(s.applyBatch(batchAddRect({0, 15}, 20, 6), "B").committed);
    REQUIRE(s.applyBatch(batchAddRect({25, -8}, 8, 12), "C").committed);
    return s;
}

std::vector<std::uint64_t> idsOf(const DocumentSession& s) {
    std::vector<std::uint64_t> ids;
    for (const auto& e : s.project().embroidery_objects) {
        ids.push_back(e.id.value);
    }
    return ids;
}

struct Mm {
    double cx, cy, w, h;
};

Mm boxOf(const DocumentSession& s, std::uint64_t id) {
    const auto* e = s.project().findEmbroidery(ObjectId{id});
    REQUIRE(e != nullptr);
    const auto g = commands::selection_geometry(s.project(), {e->source_vector});
    REQUIRE(g.has_value());
    return {to_millimeters(g->center.x).value, to_millimeters(g->center.y).value,
            to_millimeters(g->width).value, to_millimeters(g->height).value};
}

bool near(double a, double b) {
    return std::abs(a - b) < 0.01;
}

} // namespace

TEST_CASE("aligning left puts every left edge on the leftmost one in one undo step",
          "[desktop_v2][arrange]") {
    auto s = threeRects();
    const auto ids = idsOf(s);
    const auto depth = s.undoNames().size();
    const auto batch = batchAlign(s.project(), ids, AlignMode::Left);
    REQUIRE_FALSE(batch.empty());
    REQUIRE(s.applyBatch(batch, "Aligner à gauche").committed);
    CHECK(s.undoNames().size() == depth + 1);
    for (const auto id : ids) {
        const auto b = boxOf(s, id);
        CHECK(near(b.cx - b.w / 2, -35.0));
    }
    REQUIRE(s.undo());
    CHECK(near(boxOf(s, ids[1]).cx, 0.0));
    // déjà aligné : rien à faire
    REQUIRE(s.redo());
    CHECK(batchAlign(s.project(), ids, AlignMode::Left).empty());
}

TEST_CASE("aligning top and centre vertically use the union box", "[desktop_v2][arrange]") {
    auto s = threeRects();
    const auto ids = idsOf(s);
    REQUIRE(s.applyBatch(batchAlign(s.project(), ids, AlignMode::Top), "top").committed);
    for (const auto id : ids) {
        const auto b = boxOf(s, id);
        CHECK(near(b.cy + b.h / 2, 18.0));
    }
    auto t = threeRects();
    REQUIRE(t.applyBatch(batchAlign(t.project(), ids, AlignMode::CenterV), "mid").committed);
    for (const auto id : ids) {
        CHECK(near(boxOf(t, id).cy, boxOf(t, ids[0]).cy));
    }
}

TEST_CASE("distribution gives equal gaps and needs three objects", "[desktop_v2][arrange]") {
    auto s = threeRects();
    const auto ids = idsOf(s);
    CHECK(batchDistribute(s.project(), {ids[0], ids[1]}, DistributeAxis::Horizontal).empty());
    REQUIRE(
        s.applyBatch(batchDistribute(s.project(), ids, DistributeAxis::Horizontal), "d").committed);
    const auto a = boxOf(s, ids[0]);
    const auto b = boxOf(s, ids[1]);
    const auto c = boxOf(s, ids[2]);
    const double gap1 = (b.cx - b.w / 2) - (a.cx + a.w / 2);
    const double gap2 = (c.cx - c.w / 2) - (b.cx + b.w / 2);
    CHECK(near(gap1, gap2));
}

TEST_CASE("grouping and ungrouping are single undo steps", "[desktop_v2][arrange]") {
    auto s = threeRects();
    const auto ids = idsOf(s);
    CHECK(batchGroup({ids[0]}, "x").empty());
    const auto depth = s.undoNames().size();
    REQUIRE(s.applyBatch(batchGroup(ids, "Fleur \"1\""), "Grouper").committed);
    CHECK(s.undoNames().size() == depth + 1);
    CHECK(s.project().groups.size() == 1);
    const auto un = batchUngroup(s.project(), {ids[0], ids[2]});
    REQUIRE_FALSE(un.empty());
    REQUIRE(s.applyBatch(un, "Dégrouper").committed);
    CHECK(s.project().groups.empty());
    REQUIRE(s.undo());
    CHECK(s.project().groups.size() == 1);
    CHECK(batchUngroup(threeRects().project(), ids).empty());
}

TEST_CASE("numeric placement moves, resizes and rotates with limits", "[desktop_v2][arrange]") {
    auto s = threeRects();
    const auto id = idsOf(s)[0];
    PlaceRequest r;
    r.xMm = 5.0;
    r.yMm = -4.0;
    r.widthMm = 20.0;
    REQUIRE(s.applyBatch(batchPlace({id}, r), "Position").committed);
    const auto b = boxOf(s, id);
    CHECK(near(b.cx, 5.0));
    CHECK(near(b.cy, -4.0));
    CHECK(near(b.w, 20.0));
    PlaceRequest turn;
    turn.rotateDeg = 90.0;
    REQUIRE(s.applyBatch(batchPlace({id}, turn), "Rotation").committed);
    CHECK(near(boxOf(s, id).h, 20.0));
    // refus : hors limites, requête vide, aucun objet
    PlaceRequest far;
    far.xMm = 5000.0;
    CHECK(batchPlace({id}, far).empty());
    CHECK(batchPlace({id}, PlaceRequest{}).empty());
    CHECK(batchPlace({}, r).empty());
    PlaceRequest tiny;
    tiny.widthMm = 0.0;
    CHECK(batchPlace({id}, tiny).empty());
}
