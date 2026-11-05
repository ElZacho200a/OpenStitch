// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

#include "document_session.hpp"
#include "fade_axis.hpp"
#include "fade_service.hpp"
#include "openstitch/project_ops/factory.hpp"
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
        REQUIRE(session.applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle").committed);
        id = session.project().embroidery_objects.front().id.value;
    }
    void makeFade() { REQUIRE(createFade(session, id, FadeSettings{{200, 30, 30}, true}).ok); }
};
} // namespace

TEST_CASE("a plain fill has no fade axis", "[desktop_v2][fade]") {
    Fixture f;
    CHECK_FALSE(fadeAxisView(f.session.project(), f.id).has_value());
    CHECK_FALSE(
        previewFadeDrag(f.session.project(), f.id, FadeHandle::Move, {0, 0}, false).has_value());
    CHECK_FALSE(dragFadeHandle(f.session, f.id, FadeHandle::Move, {0, 0}, false).ok);
}

TEST_CASE("a fade exposes its axis, both threads and a readable summary", "[desktop_v2][fade]") {
    Fixture f;
    f.makeFade();
    const auto view = fadeAxisView(f.session.project(), f.id);
    REQUIRE(view.has_value());
    CHECK(view->background != view->foreground);
    CHECK(view->zoneStart < view->zoneEnd);
    const auto length = std::hypot(view->end.x - view->start.x, view->end.y - view->start.y);
    CHECK(length > 1.0);
    // La poignée de rotation est à 90° de l'axe, hors de la ligne.
    const double dx = view->end.x - view->start.x;
    const double dy = view->end.y - view->start.y;
    const double rx = view->rotate.x - view->centre.x;
    const double ry = view->rotate.y - view->centre.y;
    CHECK(std::abs(dx * rx + dy * ry) < 1e-6);
    CHECK(fadeSummary(*view).find("%") != std::string::npos);
}

TEST_CASE("dragging the whole axis previews without writing then commits one undo step",
          "[desktop_v2][fade]") {
    Fixture f;
    f.makeFade();
    const auto before = fadeAxisView(f.session.project(), f.id);
    REQUIRE(before.has_value());
    const PointMm target{before->centre.x + 3.0, before->centre.y + 2.0};

    const auto revision = f.session.revision();
    const auto preview =
        previewFadeDrag(f.session.project(), f.id, FadeHandle::Move, target, false);
    REQUIRE(preview.has_value());
    CHECK(f.session.revision() == revision); // un aperçu ne modifie jamais le document
    CHECK(std::abs(preview->centre.x - before->centre.x) +
              std::abs(preview->centre.y - before->centre.y) >
          0.1);

    const auto depth = f.session.undoNames().size();
    REQUIRE(dragFadeHandle(f.session, f.id, FadeHandle::Move, target, false).ok);
    CHECK(f.session.undoNames().size() == depth + 1);
    const auto after = fadeAxisView(f.session.project(), f.id);
    REQUIRE(after.has_value());
    CHECK(std::abs(after->centre.x - preview->centre.x) < 0.05);
    CHECK(std::abs(after->centre.y - preview->centre.y) < 0.05);

    REQUIRE(f.session.undo());
    const auto undone = fadeAxisView(f.session.project(), f.id);
    REQUIRE(undone.has_value());
    CHECK(std::abs(undone->centre.x - before->centre.x) < 0.01);
}

TEST_CASE("moving the end handle shortens the transition and rotation keeps its transition zone",
          "[desktop_v2][fade]") {
    Fixture f;
    f.makeFade();
    const auto before = fadeAxisView(f.session.project(), f.id);
    REQUIRE(before.has_value());
    const double lengthBefore =
        std::hypot(before->end.x - before->start.x, before->end.y - before->start.y);

    // Ramener la fin vers le centre raccourcit la zone de transition.
    const PointMm nearCentre{(before->centre.x + before->end.x) / 2.0,
                             (before->centre.y + before->end.y) / 2.0};
    const auto shorter =
        previewFadeDrag(f.session.project(), f.id, FadeHandle::End, nearCentre, false);
    REQUIRE(shorter.has_value());
    const double lengthShorter =
        std::hypot(shorter->end.x - shorter->start.x, shorter->end.y - shorter->start.y);
    CHECK(lengthShorter < lengthBefore);

    // Tourner l'axe change sa direction mais pas sa longueur.
    // La poignée est à 90° de l'axe : la tirer de côté fait pivoter l'axe.
    const PointMm above{before->rotate.x + 8.0, before->rotate.y};
    const auto turned =
        previewFadeDrag(f.session.project(), f.id, FadeHandle::Rotate, above, false);
    REQUIRE(turned.has_value());
    // Le moteur conserve la zone de transition (en fraction de la forme), pas la longueur.
    CHECK(std::abs(turned->zoneStart - before->zoneStart) < 0.05);
    CHECK(std::abs(turned->zoneEnd - before->zoneEnd) < 0.05);
    CHECK(std::abs(turned->end.y - turned->start.y) > std::abs(before->end.y - before->start.y));
}

TEST_CASE("an out-of-range drag is refused and changes nothing", "[desktop_v2][fade]") {
    Fixture f;
    f.makeFade();
    const auto depth = f.session.undoNames().size();
    CHECK_FALSE(dragFadeHandle(f.session, f.id, FadeHandle::Move, {5000.0, 0.0}, false).ok);
    CHECK(f.session.undoNames().size() == depth);
}
