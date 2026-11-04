// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "document_session.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "preview_service.hpp"
#include "shape_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
document::Project emptyProject() {
    auto p = project_ops::create_empty_project({});
    REQUIRE(p.has_value());
    return std::move(*p);
}
} // namespace

TEST_CASE("screen and model frames differ only by the Y axis", "[desktop_v2][shape]") {
    const auto p = screenToModel(12.5, 30.0);
    CHECK(p.x == 12.5);
    CHECK(p.y == -30.0);
}

TEST_CASE("shape batches refuse out-of-range input instead of clamping", "[desktop_v2][shape]") {
    CHECK(batchAddRect({0, 0}, 0.05, 10).empty());  // trop petit
    CHECK(batchAddRect({0, 0}, 10, 2500).empty());  // trop grand
    CHECK(batchAddRect({1500, 0}, 10, 10).empty()); // centre hors limites
    CHECK(batchAddEllipse({0, 0}, std::nan(""), 10).empty());
    CHECK(batchAddPolygon({{0, 0}, {1, 1}}).empty()); // moins de 3 sommets
    CHECK(batchAddPolygon({{0, 0}, {1, 1}, {5000, 2}}).empty());
    CHECK_FALSE(batchAddRect({0, 0}, 10, 10).empty());
}

TEST_CASE("names are escaped so they cannot break the batch", "[desktop_v2][shape]") {
    DocumentSession session;
    session.replaceProject(emptyProject(), true);
    const auto r =
        session.applyBatch(batchAddRect({0, 0}, 10, 10, "Ciel \"1\" \\ fin"), "Rectangle");
    REQUIRE(r.committed);
    CHECK(session.project().embroidery_objects.front().name == "Ciel \"1\" \\ fin");
}

TEST_CASE("a drawn rectangle becomes one object, one undo step, at the right place",
          "[desktop_v2][shape]") {
    DocumentSession session;
    session.replaceProject(emptyProject(), true);
    // Centre en mm « modèle » (Y vers le haut) : (10, -5) ; 20 x 8 mm.
    const auto r = session.applyBatch(batchAddRect({10, -5}, 20, 8), "Rectangle");
    REQUIRE(r.committed);
    CAPTURE(r.report.outcomes.front().result_json);
    const auto id = createdObjectId(r.report);
    REQUIRE(id.has_value());
    REQUIRE(session.project().embroidery_objects.size() == 1);
    CHECK(session.project().embroidery_objects.front().id.value == *id);
    CHECK(session.undoName() == "Rectangle");

    // Emprise réelle du rendu, en mm « écran » : centre (10, +5), 20 x 8 (aux points cousus près).
    auto preview = render_preview(session.snapshot(), session.revision(), 4.0, TaskContext{});
    REQUIRE(preview.has_value());
    REQUIRE(preview->hits != nullptr);
    const auto b = preview->hits->boundsOf(*id);
    REQUIRE(b.has_value());
    CHECK((b->x0 + b->x1) / 2.0 == Catch::Approx(10.0).margin(1.0));
    CHECK((b->y0 + b->y1) / 2.0 == Catch::Approx(5.0).margin(1.0)); // Y retourné à l'écran
    CHECK(b->x1 - b->x0 == Catch::Approx(20.0).margin(1.5));
    CHECK(b->y1 - b->y0 == Catch::Approx(8.0).margin(1.5));

    REQUIRE(session.undo());
    CHECK(session.project().embroidery_objects.empty());
}

TEST_CASE("ellipse and polygon creation, and a self-intersecting polygon is refused",
          "[desktop_v2][shape]") {
    DocumentSession session;
    session.replaceProject(emptyProject(), true);
    REQUIRE(session.applyBatch(batchAddEllipse({0, 0}, 30, 20), "Ellipse").committed);
    REQUIRE(session.applyBatch(batchAddPolygon({{-5, -5}, {5, -5}, {0, 8}}), "Polygone").committed);
    CHECK(session.project().embroidery_objects.size() == 2);
    // Papillon : auto-intersection -> le moteur refuse, le document est inchangé.
    const auto revision = session.revision();
    const auto bad =
        session.applyBatch(batchAddPolygon({{0, 0}, {10, 10}, {10, 0}, {0, 10}}), "Polygone");
    CHECK_FALSE(bad.committed);
    CHECK_FALSE(bad.report.ok);
    CHECK(session.revision() == revision);
    CHECK(session.project().embroidery_objects.size() == 2);
}

TEST_CASE("a document without stitches still has a preview of its hoop",
          "[desktop_v2][shape][preview]") {
    auto snapshot = std::make_shared<const document::Project>(emptyProject());
    const auto preview = render_preview(snapshot, 1, 2.0, TaskContext{});
    CAPTURE(preview ? std::string() : preview.error().message);
    REQUIRE(preview.has_value());
    CHECK(preview->width > 100);
    CHECK(preview->hoop_half_w_mm == Catch::Approx(50.0)); // cadre 100 x 100 par défaut
    CHECK(preview->hoop_half_h_mm == Catch::Approx(50.0));
}
