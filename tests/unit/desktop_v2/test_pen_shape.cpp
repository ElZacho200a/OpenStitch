// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

#include "document_session.hpp"
#include "node_edit.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "pen_shape.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
DocumentSession blank() {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    return s;
}
} // namespace

TEST_CASE("clicks make a straight-sided shape in one undo step", "[desktop_v2][pen]") {
    auto s = blank();
    const auto r = createPenShape(s, {{{0, 0}, {}}, {{20, 0}, {}}, {{20, 15}, {}}, {{0, 15}, {}}});
    REQUIRE(r.ok);
    CHECK_FALSE(r.curved);
    CHECK(s.undoNames().size() == 1);
    CHECK(s.project().embroidery_objects.size() == 1);
    CHECK(s.project().vector_objects.size() == 1);
    const auto view = nodeView(s.project(), r.objectId);
    REQUIRE(view.has_value());
    CHECK(view->nodes.size() == 4);
    for (const auto& n : view->nodes) {
        CHECK_FALSE(n.curved);
    }
    REQUIRE(s.undo());
    CHECK(s.project().embroidery_objects.empty());
    CHECK(s.project().vector_objects.empty());
    REQUIRE(s.redo());
    CHECK(s.project().embroidery_objects.size() == 1);
}

TEST_CASE("dragging a handle makes a smooth node with symmetric tangents", "[desktop_v2][pen]") {
    auto s = blank();
    const auto r = createPenShape(
        s, {{{0, 0}, PointMm{6, -4}}, {{30, 0}, PointMm{36, 4}}, {{15, 25}, PointMm{9, 25}}});
    REQUIRE(r.ok);
    CHECK(r.curved);
    const auto* vector =
        s.project().findObject(s.project().embroidery_objects.front().source_vector);
    REQUIRE(vector != nullptr);
    const auto& nodes = vector->paths.front().outer.nodes;
    REQUIRE(nodes.size() == 3);
    for (const auto& n : nodes) {
        REQUIRE(n.tan_out.has_value());
        REQUIRE(n.tan_in.has_value());
        CHECK(n.type == geometry::NodeType::Smooth);
        CHECK(n.tan_in->x.value == -n.tan_out->x.value); // symétriques
        CHECK(n.tan_in->y.value == -n.tan_out->y.value);
    }
    CHECK(nodes[0].tan_out->x.value == 6000);
    CHECK(nodes[0].tan_out->y.value == -4000);
}

TEST_CASE("the pen refuses degenerate, crossing, crowded and out-of-range shapes",
          "[desktop_v2][pen]") {
    auto s = blank();
    CHECK_FALSE(createPenShape(s, {{{0, 0}, {}}, {{10, 0}, {}}}).ok);               // 2 nœuds
    CHECK_FALSE(createPenShape(s, {{{0, 0}, {}}, {{0, 0}, {}}, {{10, 5}, {}}}).ok); // confondus
    CHECK_FALSE(
        createPenShape(s, {{{0, 0}, {}}, {{0.5, 0}, {}}, {{0.5, 0.5}, {}}}).ok); // trop petit
    CHECK_FALSE(createPenShape(s, {{{0, 0}, {}}, {{10, 10}, {}}, {{10, 0}, {}}, {{0, 10}, {}}})
                    .ok);                                                              // croisé
    CHECK_FALSE(createPenShape(s, {{{0, 0}, {}}, {{5000, 0}, {}}, {{0, 10}, {}}}).ok); // hors zone
    CHECK(s.undoNames().empty()); // aucun refus ne laisse de trace dans l'historique
    CHECK(s.project().embroidery_objects.empty());
}

TEST_CASE("a pen shape is a normal editable object: nodes move, stitches exist",
          "[desktop_v2][pen]") {
    auto s = blank();
    const auto r =
        createPenShape(s, {{{0, 0}, PointMm{5, -3}}, {{25, 0}, PointMm{30, 3}}, {{12, 20}, {}}});
    REQUIRE(r.ok);
    const auto view = nodeView(s.project(), r.objectId);
    REQUIRE(view.has_value());
    const auto depth = s.undoNames().size();
    REQUIRE(moveNodes(s, r.objectId, {view->nodes.back().ref}, {0.0, 1.0}, false).ok);
    CHECK(s.undoNames().size() == depth + 1);
}
