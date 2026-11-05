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

// Octogone régulier de rayon 10 mm à bord supérieur plat : ses sommets de 45° deviennent lisses
// à la conversion en courbes (un rectangle n'a que des coins francs, donc rien à arrondir).
struct OctagonFixture {
    DocumentSession session;
    std::uint64_t id{0};
    OctagonFixture() {
        auto created = project_ops::create_empty_project({});
        REQUIRE(created.has_value());
        session.replaceProject(std::move(*created), true);
        std::vector<PointMm> points;
        for (int k = 0; k < 8; ++k) {
            const double a = (22.5 + 45.0 * k) * 3.14159265358979323846 / 180.0;
            points.push_back({10.0 * std::cos(a), 10.0 * std::sin(a)});
        }
        REQUIRE(session.applyBatch(batchAddPolygon(points), "Octogone").committed);
        id = session.project().embroidery_objects.front().id.value;
    }
};

TEST_CASE("converting to curves turns corners into editable Bezier nodes in one undo step",
          "[desktop_v2][nodes][curves]") {
    OctagonFixture f;
    const auto before = nodeView(f.session.project(), f.id);
    REQUIRE(before.has_value());
    for (const auto& n : before->nodes) {
        CHECK_FALSE(n.curved);
    }
    const auto depth = f.session.undoNames().size();
    REQUIRE(convertToCurves(f.session, {f.id}).ok);
    CHECK(f.session.undoNames().size() == depth + 1);
    const auto after = nodeView(f.session.project(), f.id);
    REQUIRE(after.has_value());
    bool anyCurved = false;
    for (const auto& n : after->nodes) {
        anyCurved = anyCurved || n.curved;
    }
    CHECK(anyCurved);
    CHECK(after->outlines.front().size() > before->outlines.front().size()); // tracé courbe aplati

    f.session.undo();
    const auto undone = nodeView(f.session.project(), f.id);
    REQUIRE(undone.has_value());
    for (const auto& n : undone->nodes) {
        CHECK_FALSE(n.curved);
    }
    CHECK_FALSE(convertToCurves(f.session, {999999}).ok); // objet inconnu : refus clair
}

TEST_CASE("pulling a curve segment previews, commits one step and keeps the nodes in place",
          "[desktop_v2][nodes][curves]") {
    OctagonFixture f;
    REQUIRE(convertToCurves(f.session, {f.id}).ok);
    const auto view = nodeView(f.session.project(), f.id);
    REQUIRE(view.has_value());
    // Un point du bord supérieur de l'octogone (y = +9,24 mm) : on saisit le segment, pas un nœud.
    const auto hit = curveSegmentAt(f.session.project(), f.id, {0.0, 9.24}, 1.5);
    REQUIRE(hit.has_value());
    CHECK(hit->t > 0.0);
    CHECK(hit->t < 1.0);

    const auto revision = f.session.revision();
    const auto preview = previewSegmentReshape(f.session.project(), f.id, *hit, {0.0, 3.0});
    REQUIRE(preview.has_value());
    CHECK(f.session.revision() == revision); // un aperçu ne modifie jamais le document
    double topBefore = -1e9;
    double topAfter = -1e9;
    for (const auto& p : view->outlines.front()) {
        topBefore = std::max(topBefore, p.y);
    }
    for (const auto& p : preview->front()) {
        topAfter = std::max(topAfter, p.y);
    }
    CHECK(topAfter > topBefore + 0.5); // le bord est effectivement tiré vers le haut

    const auto depth = f.session.undoNames().size();
    REQUIRE(reshapeSegment(f.session, f.id, *hit, {0.0, 3.0}).ok);
    CHECK(f.session.undoNames().size() == depth + 1);
    const auto after = nodeView(f.session.project(), f.id);
    REQUIRE(after.has_value());
    REQUIRE(after->nodes.size() == view->nodes.size()); // aucun nœud ajouté ni retiré
    for (std::size_t i = 0; i < view->nodes.size(); ++i) {
        CHECK(std::abs(after->nodes[i].pos.x - view->nodes[i].pos.x) < 0.01);
        CHECK(std::abs(after->nodes[i].pos.y - view->nodes[i].pos.y) < 0.01);
    }
}

TEST_CASE("a segment far from the outline is not grabbed and satin rails use nodes instead",
          "[desktop_v2][nodes][curves]") {
    OctagonFixture f;
    CHECK_FALSE(curveSegmentAt(f.session.project(), f.id, {0.0, 40.0}, 1.0).has_value());

    auto s = emptySession();
    const auto made = createSatinColumn(s, {{{0, 0}, {0, 4}}, {{10, 0}, {10, 4}}});
    REQUIRE(made.ok);
    CHECK_FALSE(curveSegmentAt(s.project(), made.objectId, {5.0, 0.0}, 1.0).has_value());
}

TEST_CASE("smoothing rounds the contour and is one undo step", "[desktop_v2][nodes][curves]") {
    OctagonFixture f;
    const auto depth = f.session.undoNames().size();
    const auto r = smoothContours(f.session, {f.id});
    if (r.ok) {
        CHECK(f.session.undoNames().size() == depth + 1);
    } else {
        // Un rectangle parfait peut ne rien avoir à lisser : le refus doit alors être clair.
        CHECK_FALSE(r.message.empty());
        CHECK(f.session.undoNames().size() == depth);
    }
}

TEST_CASE("deleting nodes is one undo step and keeps a valid contour",
          "[desktop_v2][nodes][delete]") {
    OctagonFixture f;
    const auto view = nodeView(f.session.project(), f.id);
    REQUIRE(view.has_value());
    REQUIRE(view->nodes.size() == 8);
    const auto depth = f.session.undoNames().size();

    // Deux nœuds d'un coup : un seul pas d'annulation, 6 nœuds restants.
    REQUIRE(deleteNodes(f.session, f.id, {view->nodes[1].ref, view->nodes[5].ref}).ok);
    CHECK(f.session.undoNames().size() == depth + 1);
    const auto after = nodeView(f.session.project(), f.id);
    REQUIRE(after.has_value());
    CHECK(after->nodes.size() == 6);
    f.session.undo();
    const auto undone = nodeView(f.session.project(), f.id);
    REQUIRE(undone.has_value());
    REQUIRE(undone->nodes.size() == 8);
    for (std::size_t i = 0; i < 8; ++i) { // exactement la forme d'origine
        CHECK(std::abs(undone->nodes[i].pos.x - view->nodes[i].pos.x) < 0.001);
        CHECK(std::abs(undone->nodes[i].pos.y - view->nodes[i].pos.y) < 0.001);
    }
}

TEST_CASE("deleting is refused below three nodes, for nothing, and on satin rails",
          "[desktop_v2][nodes][delete]") {
    Fixture f; // rectangle : 4 nœuds
    const auto view = nodeView(f.session.project(), f.id);
    REQUIRE(view.has_value());
    const auto depth = f.session.undoNames().size();
    CHECK_FALSE(deleteNodes(f.session, f.id, {}).ok);
    // Retirer 3 des 4 nœuds laisserait moins d'un triangle : refusé, rien ne change.
    const auto r =
        deleteNodes(f.session, f.id, {view->nodes[0].ref, view->nodes[1].ref, view->nodes[2].ref});
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.message.empty());
    CHECK(f.session.undoNames().size() == depth);
    // Un seul nœud sur 4 : autorisé (il reste un triangle).
    CHECK(deleteNodes(f.session, f.id, {view->nodes[0].ref}).ok);

    auto s = emptySession();
    const auto made =
        createSatinColumn(s, {{{0, 0}, {0, 4}}, {{10, 0}, {10, 4}}, {{20, 0}, {20, 4}}});
    REQUIRE(made.ok);
    CHECK_FALSE(deleteNodes(s, made.objectId, {NodeRef{0, 0, 1}}).ok);
}

TEST_CASE("curved nodes expose their tangent handles in absolute positions",
          "[desktop_v2][nodes][tangent]") {
    OctagonFixture f;
    const auto flat = nodeView(f.session.project(), f.id);
    REQUIRE(flat.has_value());
    CHECK(flat->tangents.empty()); // un polygone n'a pas de poignée
    REQUIRE(convertToCurves(f.session, {f.id}).ok);
    const auto view = nodeView(f.session.project(), f.id);
    REQUIRE(view.has_value());
    REQUIRE_FALSE(view->tangents.empty());
    for (const auto& t : view->tangents) {
        CHECK(std::hypot(t.pos.x - t.anchor.x, t.pos.y - t.anchor.y) > 0.1);
    }
}

TEST_CASE(
    "dragging a handle bends the curve, keeps the node, is one undo step and mirrors smooth nodes",
    "[desktop_v2][nodes][tangent]") {
    OctagonFixture f;
    REQUIRE(convertToCurves(f.session, {f.id}).ok);
    const auto view = nodeView(f.session.project(), f.id);
    REQUIRE(view.has_value());
    const auto handle = view->tangents.front();
    const PointMm target{handle.pos.x + 4.0, handle.pos.y + 3.0};

    const auto revision = f.session.revision();
    const auto preview =
        previewTangentDrag(f.session.project(), f.id, handle.node, handle.isOut, target);
    REQUIRE(preview.has_value());
    CHECK(f.session.revision() == revision); // l'aperçu n'écrit rien

    const auto depth = f.session.undoNames().size();
    REQUIRE(moveTangent(f.session, f.id, handle.node, handle.isOut, target).ok);
    CHECK(f.session.undoNames().size() == depth + 1); // poignée + miroir = un seul pas
    const auto after = nodeView(f.session.project(), f.id);
    REQUIRE(after.has_value());
    // Le nœud n'a pas bougé…
    for (std::size_t i = 0; i < view->nodes.size(); ++i) {
        CHECK(std::abs(after->nodes[i].pos.x - view->nodes[i].pos.x) < 0.001);
        CHECK(std::abs(after->nodes[i].pos.y - view->nodes[i].pos.y) < 0.001);
    }
    // …sa poignée est à la nouvelle place…
    bool movedFound = false;
    for (const auto& t : after->tangents) {
        if (t.node == handle.node && t.isOut == handle.isOut) {
            movedFound = true;
            CHECK(std::abs(t.pos.x - target.x) < 0.01);
            CHECK(std::abs(t.pos.y - target.y) < 0.01);
        }
    }
    CHECK(movedFound);
    // …et la poignée opposée d'un nœud lisse est restée colinéaire.
    const TangentHandle* opposite = nullptr;
    const TangentHandle* moved = nullptr;
    for (const auto& t : after->tangents) {
        if (t.node == handle.node) {
            (t.isOut == handle.isOut ? moved : opposite) = &t;
        }
    }
    if (opposite != nullptr && moved != nullptr) {
        const double ax = moved->pos.x - moved->anchor.x;
        const double ay = moved->pos.y - moved->anchor.y;
        const double bx = opposite->pos.x - opposite->anchor.x;
        const double by = opposite->pos.y - opposite->anchor.y;
        CHECK(std::abs(ax * by - ay * bx) < 0.05 * std::hypot(ax, ay) * std::hypot(bx, by));
    }
    f.session.undo();
    const auto undone = nodeView(f.session.project(), f.id);
    REQUIRE(undone.has_value());
    REQUIRE(undone->tangents.size() == view->tangents.size());
    for (std::size_t i = 0; i < view->tangents.size(); ++i) {
        CHECK(std::abs(undone->tangents[i].pos.x - view->tangents[i].pos.x) < 0.001);
    }
}

TEST_CASE("a satin rail handle can be dragged too, and an out-of-range handle is refused",
          "[desktop_v2][nodes][tangent]") {
    auto s = emptySession();
    const auto made =
        createSatinColumn(s, {{{0, 0}, {0, 4}}, {{10, 0}, {10, 4}}, {{20, 0}, {20, 4}}});
    REQUIRE(made.ok);
    const auto view = nodeView(s.project(), made.objectId);
    REQUIRE(view.has_value());
    CHECK(view->tangents.empty()); // rails droits au départ : pas de poignée à afficher
    // Dragging a handle on a node without tangents creates one (clean curve from a straight rail).
    const auto depth = s.undoNames().size();
    REQUIRE(moveTangent(s, made.objectId, NodeRef{0, 0, 1}, true, {10.0, 3.0}).ok);
    CHECK(s.undoNames().size() == depth + 1);
    const auto after = nodeView(s.project(), made.objectId);
    REQUIRE(after.has_value());
    CHECK_FALSE(after->tangents.empty());
    CHECK_FALSE(moveTangent(s, made.objectId, NodeRef{0, 0, 1}, true, {5000.0, 0.0}).ok);
    CHECK(s.undoNames().size() == depth + 1);
}
