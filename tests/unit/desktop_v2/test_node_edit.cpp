// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

#include "document_session.hpp"
#include "node_edit.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "satin_column.hpp"
#include "shape_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
struct Fixture {
    DocumentSession session;
    std::uint64_t id{0};
    Fixture() {
        auto created = project_ops::create_empty_project({});
        REQUIRE(created.has_value());
        session.replaceProject(std::move(*created), true);
        REQUIRE(session.applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle").committed);
        REQUIRE(!session.project().embroidery_objects.empty());
        id = session.project().embroidery_objects.front().id.value;
    }
};

[[nodiscard]] const NodeHandle* topRight(const NodeView& v) {
    const NodeHandle* best = nullptr;
    for (const auto& n : v.nodes) {
        if (best == nullptr || n.pos.x + n.pos.y > best->pos.x + best->pos.y) {
            best = &n;
        }
    }
    return best;
}
} // namespace

TEST_CASE("a rectangle exposes its four corner nodes and one outline", "[desktop_v2][nodes]") {
    Fixture f;
    const auto view = nodeView(f.session.project(), f.id);
    REQUIRE(view.has_value());
    CHECK(view->nodes.size() == 4);
    CHECK(view->outlines.size() == 1);
    CHECK(view->outlines.front().size() >= 5); // fermé : le premier point est répété
}

TEST_CASE("an unknown object has no editable nodes", "[desktop_v2][nodes]") {
    Fixture f;
    CHECK_FALSE(nodeView(f.session.project(), 999999).has_value());
}

TEST_CASE("moving a node previews without touching the document then commits one undo step",
          "[desktop_v2][nodes]") {
    Fixture f;
    const auto view = nodeView(f.session.project(), f.id);
    REQUIRE(view.has_value());
    const auto* corner = topRight(*view);
    REQUIRE(corner != nullptr);

    const auto revision = f.session.revision();
    const auto preview =
        previewNodeMove(f.session.project(), f.id, {corner->ref}, {3.0, 2.0}, false);
    REQUIRE(preview.has_value());
    CHECK(f.session.revision() == revision); // un aperçu ne modifie jamais le document

    const auto depth = f.session.undoNames().size();
    const auto r = moveNodes(f.session, f.id, {corner->ref}, {3.0, 2.0}, false);
    REQUIRE(r.ok);
    CHECK(f.session.undoNames().size() == depth + 1);
    const auto after = nodeView(f.session.project(), f.id);
    REQUIRE(after.has_value());
    const auto* moved = topRight(*after);
    CHECK(std::abs(moved->pos.x - (corner->pos.x + 3.0)) < 0.01);
    CHECK(std::abs(moved->pos.y - (corner->pos.y + 2.0)) < 0.01);

    f.session.undo();
    const auto undone = nodeView(f.session.project(), f.id);
    REQUIRE(undone.has_value());
    CHECK(std::abs(topRight(*undone)->pos.x - corner->pos.x) < 0.01);
}

TEST_CASE("a move that would make the outline cross itself is refused", "[desktop_v2][nodes]") {
    Fixture f;
    const auto view = nodeView(f.session.project(), f.id);
    REQUIRE(view.has_value());
    const auto* corner = topRight(*view);
    // Descendre le coin haut-droit sous le bord inférieur fait croiser deux côtés du contour.
    const auto preview =
        previewNodeMove(f.session.project(), f.id, {corner->ref}, {0.0, -25.0}, false);
    CHECK_FALSE(preview.has_value());
    const auto depth = f.session.undoNames().size();
    CHECK_FALSE(moveNodes(f.session, f.id, {corner->ref}, {0.0, -25.0}, false).ok);
    CHECK(f.session.undoNames().size() == depth);
}

TEST_CASE("a node can be inserted on a segment without changing the shape", "[desktop_v2][nodes]") {
    Fixture f;
    const auto view = nodeView(f.session.project(), f.id);
    REQUIRE(view.has_value());
    // Un point au milieu du bord supérieur (y = +5 mm).
    const auto hit = nearestSegment(*view, {0.0, 5.0}, 1.0);
    REQUIRE(hit.has_value());
    CHECK(hit->t > 0.3);
    CHECK(hit->t < 0.7);
    REQUIRE(insertNode(f.session, f.id, *hit).ok);
    const auto after = nodeView(f.session.project(), f.id);
    REQUIRE(after.has_value());
    CHECK(after->nodes.size() == 5);
    CHECK_FALSE(nearestSegment(*view, {0.0, 50.0}, 1.0).has_value()); // trop loin d'un bord
}

namespace {
DocumentSession emptySession() {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    return s;
}
} // namespace

TEST_CASE("a satin column exposes both rails and moves a rail node in one undo step",
          "[desktop_v2][nodes][satin]") {
    auto s = emptySession();
    const auto made =
        createSatinColumn(s, {{{0, 0}, {0, 4}}, {{10, 0}, {10, 4}}, {{20, 0}, {20, 4}}});
    REQUIRE(made.ok);
    const auto view = nodeView(s.project(), made.objectId);
    REQUIRE(view.has_value());
    CHECK(view->satin);
    CHECK(view->nodes.size() == 6);
    CHECK(view->outlines.size() == 2);
    CHECK(view->rungs.size() == 3);
    CHECK(satinWidthSummary(*view).find("4.0") != std::string::npos);

    const auto depth = s.undoNames().size();
    const NodeRef a1{0, 0, 1};
    const NodeRef a2{0, 0, 2};
    REQUIRE(previewNodeMove(s.project(), made.objectId, {a1, a2}, {0.0, 1.5}, false).has_value());
    CHECK(s.undoNames().size() == depth); // l'aperçu n'écrit rien
    REQUIRE(moveNodes(s, made.objectId, {a1, a2}, {0.0, 1.5}, false).ok);
    CHECK(s.undoNames().size() == depth + 1); // deux nœuds, un seul pas d'annulation
    const auto moved = nodeView(s.project(), made.objectId);
    REQUIRE(moved.has_value());
    CHECK(std::abs(moved->nodes[1].pos.y - (view->nodes[1].pos.y + 1.5)) < 0.01);
    CHECK(std::abs(moved->nodes[2].pos.y - (view->nodes[2].pos.y + 1.5)) < 0.01);
    CHECK(std::abs(moved->nodes[0].pos.y - view->nodes[0].pos.y) < 0.01);
    s.undo();
    const auto restored = nodeView(s.project(), made.objectId);
    REQUIRE(restored.has_value());
    CHECK(std::abs(restored->nodes[1].pos.y - view->nodes[1].pos.y) < 0.01);

    // Deux rails à la fois dans un même geste : refusé (un geste = un rail).
    CHECK_FALSE(
        moveNodes(s, made.objectId, {NodeRef{0, 0, 0}, NodeRef{1, 0, 0}}, {1, 1}, false).ok);
}

TEST_CASE("a node can be inserted on an open rail but not past its last node",
          "[desktop_v2][nodes][satin]") {
    auto s = emptySession();
    const auto made = createSatinColumn(s, {{{0, 0}, {0, 4}}, {{10, 0}, {10, 4}}});
    REQUIRE(made.ok);
    const auto view = nodeView(s.project(), made.objectId);
    REQUIRE(view.has_value());
    const auto hit = nearestSegment(*view, {5.0, 0.0}, 1.0);
    REQUIRE(hit.has_value());
    CHECK(hit->start.set == 0);
    REQUIRE(insertNode(s, made.objectId, *hit).ok);
    const auto after = nodeView(s.project(), made.objectId);
    REQUIRE(after.has_value());
    CHECK(after->nodes.size() == 5);
    // Au-delà de l'extrémité d'un rail ouvert, aucun segment « de retour » ne doit être proposé.
    CHECK_FALSE(nearestSegment(*after, {-6.0, 0.0}, 1.0).has_value());
}

TEST_CASE("a too wide satin is reported with advice", "[desktop_v2][nodes][satin]") {
    auto s = emptySession();
    const auto made = createSatinColumn(s, {{{0, 0}, {0, 14}}, {{10, 0}, {10, 14}}});
    REQUIRE(made.ok);
    const auto view = nodeView(s.project(), made.objectId);
    REQUIRE(view.has_value());
    const auto summary = satinWidthSummary(*view);
    CHECK(summary.find("tatami") != std::string::npos);
    CHECK(satinWidthSummary(NodeView{}).empty()); // pas un satin : rien à dire
}
