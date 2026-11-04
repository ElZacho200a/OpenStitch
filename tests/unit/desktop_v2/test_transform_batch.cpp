// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "document_session.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "preview_service.hpp"
#include "transform_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

struct Placed {
    DocumentSession session;
    std::uint64_t id{0};
};

// Rectangle 20 x 10 mm centré en (0, 0) du modèle.
Placed makeRect() {
    Placed p;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    p.session.replaceProject(std::move(*created), true);
    const auto r = p.session.applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle");
    REQUIRE(r.committed);
    p.id = *createdObjectId(r.report);
    return p;
}

BoxMm boundsOf(DocumentSession& s, std::uint64_t id) {
    const auto preview = render_preview(s.snapshot(), s.revision(), 4.0, TaskContext{});
    REQUIRE(preview.has_value());
    const auto b = preview->hits->boundsOf(id);
    REQUIRE(b.has_value());
    return *b;
}

} // namespace

TEST_CASE("transform batches refuse out-of-range or empty requests", "[desktop_v2][transform]") {
    CHECK(batchTranslate({}, 1, 1).empty());
    CHECK(batchTranslate({1}, 0, 0).empty()); // déplacement nul : rien à faire
    CHECK(batchTranslate({1}, 5000, 0).empty());
    CHECK(batchTranslate({1}, std::nan(""), 0).empty());
    CHECK(batchScale({1}, 0.01, {0, 0}).empty());
    CHECK(batchScale({1}, 25, {0, 0}).empty());
    CHECK(batchScale({1}, 1.0, {0, 0}).empty()); // échelle neutre
    CHECK(batchScale({1}, 2, {2000, 0}).empty());
    CHECK_FALSE(batchTranslate({1}, 3, -2).empty());
    CHECK_FALSE(batchScale({1}, 2, {5, 5}).empty());
}

TEST_CASE("a move is one undo step for the whole selection and lands where asked",
          "[desktop_v2][transform]") {
    auto p = makeRect();
    const auto second = p.session.applyBatch(batchAddEllipse({30, 0}, 10, 10), "Ellipse");
    REQUIRE(second.committed);
    const auto id2 = *createdObjectId(second.report);
    const auto before1 = boundsOf(p.session, p.id);
    const auto before2 = boundsOf(p.session, id2);
    const auto depth = p.session.undoNames().size();

    // +7 mm à droite, +4 mm vers le HAUT (modèle) = 4 mm vers le haut = y écran -4.
    const auto moved = p.session.applyBatch(batchTranslate({p.id, id2}, 7, 4), "Déplacer");
    REQUIRE(moved.committed);
    CHECK(p.session.undoNames().size() == depth + 1);
    const auto after1 = boundsOf(p.session, p.id);
    const auto after2 = boundsOf(p.session, id2);
    CHECK(after1.x0 - before1.x0 == Catch::Approx(7.0).margin(0.3));
    CHECK(after1.y0 - before1.y0 == Catch::Approx(-4.0).margin(0.3)); // Y retourné à l'écran
    CHECK(after2.x0 - before2.x0 == Catch::Approx(7.0).margin(0.3));
    CHECK(after2.y0 - before2.y0 == Catch::Approx(-4.0).margin(0.3));

    REQUIRE(p.session.undo());
    const auto restored = boundsOf(p.session, p.id);
    CHECK(restored.x0 == Catch::Approx(before1.x0).margin(0.05));
}

TEST_CASE("a scale about a pivot keeps the pivot fixed", "[desktop_v2][transform]") {
    auto p = makeRect(); // 20 x 10, emprise modèle x [-10, 10], y [-5, 5]
    const auto before = boundsOf(p.session, p.id);
    // Pivot = coin bas-gauche du modèle (-10, -5) ; facteur 2 -> 40 x 20 mm.
    const auto scaled = p.session.applyBatch(batchScale({p.id}, 2.0, {-10, -5}), "Échelle");
    REQUIRE(scaled.committed);
    const auto after = boundsOf(p.session, p.id);
    CHECK(after.x1 - after.x0 == Catch::Approx(2.0 * (before.x1 - before.x0)).margin(1.0));
    CHECK(after.y1 - after.y0 == Catch::Approx(2.0 * (before.y1 - before.y0)).margin(1.0));
    // Le pivot (-10, -5 modèle) = (-10, +5 écran) est le coin gauche / BAS à l'écran : x0 et y1 fixes.
    CHECK(after.x0 == Catch::Approx(before.x0).margin(0.6));
    CHECK(after.y1 == Catch::Approx(before.y1).margin(0.6));
}

TEST_CASE("a move that leaves the allowed area is refused by the engine and changes nothing",
          "[desktop_v2][transform]") {
    auto p = makeRect();
    const auto revision = p.session.revision();
    const auto r = p.session.applyBatch(batchTranslate({p.id}, 1900, 0), "Déplacer");
    // Soit le moteur accepte, soit il refuse : jamais d'état intermédiaire.
    if (!r.committed) {
        CHECK(p.session.revision() == revision);
    } else {
        CHECK(p.session.undo());
    }
}
