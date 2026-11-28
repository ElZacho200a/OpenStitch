// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "document_session.hpp"
#include "edit_batch.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "preview_service.hpp"

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

std::uint64_t addRect(DocumentSession& s, PointMm c, double w, double h) {
    const auto r = s.applyBatch(batchAddRect(c, w, h), "Rectangle");
    REQUIRE(r.committed);
    return *createdObjectId(r.report);
}

BoxMm boundsOf(DocumentSession& s, std::uint64_t id) {
    const auto preview = render_preview(s.snapshot(), s.revision(), 4.0, TaskContext{});
    REQUIRE(preview.has_value());
    const auto b = preview->hits->boundsOf(id);
    REQUIRE(b.has_value());
    return *b;
}

} // namespace

TEST_CASE("edit batches refuse empty or out-of-range requests", "[desktop_v2][edit]") {
    CHECK(batchRemove({}).empty());
    CHECK(batchDuplicate({1}, {5000, 0}).empty());
    CHECK(batchRotate({1}, 400, {0, 0}).empty());
    CHECK(batchRotate({1}, 0, {0, 0}).empty());
    CHECK(batchRotate({1}, 45, {0, 5000}).empty());
    CHECK(batchMirror({}, MirrorAxis::Horizontal, {0, 0}).empty());
    CHECK(batchStack({1}, "sideways").empty());
    CHECK_FALSE(batchStack({1, 2}, "front").empty());
}

TEST_CASE("remove is one undo step for the whole selection", "[desktop_v2][edit]") {
    auto s = newSession();
    const auto a = addRect(s, {-20, 0}, 10, 10);
    const auto b = addRect(s, {20, 0}, 10, 10);
    const auto depth = s.undoNames().size();
    REQUIRE(s.applyBatch(batchRemove({a, b}), "Supprimer").committed);
    CHECK(s.project().embroidery_objects.empty());
    CHECK(s.undoNames().size() == depth + 1);
    REQUIRE(s.undo());
    CHECK(s.project().embroidery_objects.size() == 2);
}

TEST_CASE("remove with source deletes the shape with its last stitch layer", "[desktop_v2][edit]") {
    auto s = newSession();
    const auto a = addRect(s, {-20, 0}, 10, 10);
    const auto shapes = s.project().vector_objects.size();
    // Sans with_source : la forme reste, sans points.
    REQUIRE(s.applyBatch(batchRemove({a}), "Retirer les points").committed);
    CHECK(s.project().vector_objects.size() == shapes);
    REQUIRE(s.undo());
    // Avec : la forme part avec sa dernière couche, en une étape.
    const auto depth = s.undoNames().size();
    REQUIRE(s.applyBatch(batchRemove({a}, true), "Supprimer").committed);
    CHECK(s.project().embroidery_objects.empty());
    CHECK(s.project().vector_objects.size() == shapes - 1);
    CHECK(s.undoNames().size() == depth + 1);
}

TEST_CASE("duplicate creates offset copies and reports every created id", "[desktop_v2][edit]") {
    auto s = newSession();
    const auto a = addRect(s, {-20, 0}, 10, 10);
    const auto b = addRect(s, {20, 0}, 10, 10);
    const auto before = boundsOf(s, a);
    const auto r = s.applyBatch(batchDuplicate({a, b}, {5, -5}), "Dupliquer");
    REQUIRE(r.committed);
    CHECK(s.project().embroidery_objects.size() == 4);
    const auto created = createdObjectIds(r.report);
    REQUIRE(created.size() == 2);
    CHECK(created[0] != a);
    CHECK(created[1] != b);
    const auto copy = boundsOf(s, created[0]);
    // +5 mm à droite, -5 mm modèle = +5 mm vers le bas à l'écran.
    CHECK(copy.x0 - before.x0 == Catch::Approx(5.0).margin(0.4));
    CHECK(copy.y0 - before.y0 == Catch::Approx(5.0).margin(0.4));
    CHECK(s.undoNames().size() == 3); // deux créations + un seul « Dupliquer »
}

TEST_CASE("a 90 degree rotation about the centre swaps width and height", "[desktop_v2][edit]") {
    auto s = newSession();
    const auto id = addRect(s, {0, 0}, 30, 10);
    const auto before = boundsOf(s, id);
    REQUIRE(s.applyBatch(batchRotate({id}, 90, {0, 0}), "Pivoter").committed);
    const auto after = boundsOf(s, id);
    CHECK(after.x1 - after.x0 == Catch::Approx(before.y1 - before.y0).margin(1.0));
    CHECK(after.y1 - after.y0 == Catch::Approx(before.x1 - before.x0).margin(1.0));
    CHECK((after.x0 + after.x1) / 2 == Catch::Approx(0.0).margin(0.6)); // centre inchangé
}

TEST_CASE("a horizontal mirror about the origin flips left and right", "[desktop_v2][edit]") {
    auto s = newSession();
    const auto id = addRect(s, {20, 0}, 10, 10);
    const auto before = boundsOf(s, id);
    REQUIRE(s.applyBatch(batchMirror({id}, MirrorAxis::Horizontal, {0, 0}), "Miroir").committed);
    const auto after = boundsOf(s, id);
    CHECK((after.x0 + after.x1) / 2 == Catch::Approx(-(before.x0 + before.x1) / 2).margin(0.6));
    CHECK(after.y0 == Catch::Approx(before.y0).margin(0.6));
}

TEST_CASE("stack order moves an object to the front of the sewing order", "[desktop_v2][edit]") {
    auto s = newSession();
    const auto a = addRect(s, {-20, 0}, 10, 10);
    (void)addRect(s, {0, 0}, 10, 10);
    const auto c = addRect(s, {20, 0}, 10, 10);
    CHECK(s.project().embroidery_objects.front().id.value == a);
    REQUIRE(s.applyBatch(batchStack({a}, "front"), "Premier plan").committed);
    CHECK(s.project().embroidery_objects.back().id.value == a);
    CHECK(s.project().embroidery_objects.front().id.value != a);
    REQUIRE(s.applyBatch(batchStack({c}, "back"), "Arrière-plan").committed);
    CHECK(s.project().embroidery_objects.front().id.value == c);
}

TEST_CASE("a knife line splits a shape into independent objects in one undo step",
          "[desktop_v2][edit]") {
    auto s = newSession();
    const auto id = addRect(s, {0, 0}, 40, 20);
    CHECK(batchSplit({id}, {0, 0}, {0, 0}).empty()); // points confondus
    CHECK(batchSplit({id}, {0, 0}, {5000, 0}).empty());
    CHECK(batchSplit({}, {0, -20}, {0, 20}).empty());
    const auto depth = s.undoNames().size();
    const auto r = s.applyBatch(batchSplit({id}, {0, -20}, {0, 20}), "Découper");
    REQUIRE(r.committed);
    CHECK(s.undoNames().size() == depth + 1);
    CHECK(s.project().embroidery_objects.size() == 2);
    REQUIRE(s.undo());
    CHECK(s.project().embroidery_objects.size() == 1);
    // Une ligne qui ne traverse rien est refusée par le moteur : rien n'est empilé.
    const auto miss = s.applyBatch(batchSplit({id}, {100, -20}, {100, 20}), "Découper");
    CHECK_FALSE(miss.committed);
}

TEST_CASE("edit batch: split_path coupe selon un trace libre, une etape d'annulation",
          "[desktop_v2][edit]") {
    auto s = newSession();
    const auto id = addRect(s, {0, 0}, 40, 20);
    CHECK(batchSplitPath({id}, {}).empty());
    CHECK(batchSplitPath({id}, {{0, 0}}).empty());
    CHECK(batchSplitPath({id}, {{0, 0}, {0, 0}}).empty()); // aucun trace
    CHECK(batchSplitPath({}, {{0, -20}, {0, 20}}).empty());
    const auto depth = s.undoNames().size();
    // Un trace qui entre et s'arrete dans la forme n'est qu'une entaille : refuse, rien d'empile.
    const auto blind = s.applyBatch(batchSplitPath({id}, {{-5, 15}, {0, 0}}), "Découper");
    CHECK_FALSE(blind.committed);
    CHECK(s.undoNames().size() == depth);
    const auto cut = s.applyBatch(batchSplitPath({id}, {{-5, 15}, {0, 0}, {5, -15}}), "Découper");
    REQUIRE(cut.committed);
    CHECK(s.undoNames().size() == depth + 1);
    CHECK(s.project().embroidery_objects.size() == 2);
    REQUIRE(s.undo());
    CHECK(s.project().embroidery_objects.size() == 1);
}

TEST_CASE("edit batch: simplifyPath garde extremites et coins, retire le bruit",
          "[desktop_v2][edit]") {
    // Ligne droite bruitee puis un coin franc.
    std::vector<PointMm> path;
    for (int i = 0; i <= 20; ++i) {
        path.push_back({static_cast<double>(i), (i % 2 == 0 ? 0.01 : -0.01)});
    }
    for (int j = 1; j <= 10; ++j) {
        path.push_back({20.0, static_cast<double>(j)});
    }
    const auto out = simplifyPath(path, 0.1);
    REQUIRE(out.size() == 3);
    CHECK(out.front().x == Catch::Approx(0.0));
    CHECK(out[1].x == Catch::Approx(20.0));
    CHECK(out.back().y == Catch::Approx(10.0));
    // Tolerance nulle ou trace court : inchange ; deterministe.
    CHECK(simplifyPath(path, 0.0).size() == path.size());
    CHECK(simplifyPath({{0, 0}, {1, 1}}, 1.0).size() == 2);
    CHECK(simplifyPath(path, 0.1).size() == out.size());
}
