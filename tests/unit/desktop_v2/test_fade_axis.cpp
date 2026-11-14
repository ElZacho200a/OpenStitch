// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <memory>

#include "document_session.hpp"
#include "fade_axis.hpp"
#include "fade_service.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/project_ops/gradient_fill.hpp"
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

TEST_CASE("a multicolor fade angle rotates one shared axis and undo restores it",
          "[desktop_v2][fade][multicolor]") {
    Fixture f;
    MultiFadeSettings settings;
    settings.colors = {{{20, 60, 120}}, {{90, 140, 190}}, {{220, 235, 245}}};
    settings.geometry = 2; // curved fields also have an explicit orientation
    REQUIRE(createMultiFade(f.session, f.id, settings).ok);
    const auto beforeAngle = readFadeAngle(f.session.project(), f.id);
    REQUIRE(beforeAngle.has_value());
    const auto before =
        project_ops::describe_multicolor_gradient(f.session.project(), ObjectId{f.id});
    REQUIRE(before.has_value());
    const double beforeLength = std::hypot(
        static_cast<double>(before->transition.to.x.value - before->transition.from.x.value),
        static_cast<double>(before->transition.to.y.value - before->transition.from.y.value));
    const auto depth = f.session.undoNames().size();

    REQUIRE(setFadeAngle(f.session, f.id, 90.0).ok);
    CHECK(f.session.undoNames().size() == depth + 1);
    REQUIRE(readFadeAngle(f.session.project(), f.id).has_value());
    CHECK(std::abs(*readFadeAngle(f.session.project(), f.id) - 90.0) < 0.01);
    const auto after =
        project_ops::describe_multicolor_gradient(f.session.project(), ObjectId{f.id});
    REQUIRE(after.has_value());
    const double afterLength = std::hypot(
        static_cast<double>(after->transition.to.x.value - after->transition.from.x.value),
        static_cast<double>(after->transition.to.y.value - after->transition.from.y.value));
    CHECK(std::abs(afterLength - beforeLength) < 2.0); // arrondi entier en micromètres

    REQUIRE(f.session.undo());
    CHECK(std::abs(*readFadeAngle(f.session.project(), f.id) - *beforeAngle) < 0.01);
    CHECK_FALSE(setFadeAngle(f.session, f.id, std::numeric_limits<double>::infinity()).ok);
}

TEST_CASE("every fade curve runs from 0 to 1 monotonically and has a label",
          "[desktop_v2][fade][curve]") {
    for (const auto curve : {FadeCurve::Linear, FadeCurve::Smooth, FadeCurve::Steep,
                             FadeCurve::EaseIn, FadeCurve::EaseOut}) {
        INFO(fadeCurveLabel(curve));
        CHECK_FALSE(fadeCurveLabel(curve).empty());
        CHECK(fadeCurveValue(curve, 0.0) == 0.0);
        CHECK(std::abs(fadeCurveValue(curve, 1.0) - 1.0) < 1e-12);
        const auto profile = fadeCurveProfile(curve, 33);
        REQUIRE(profile.size() == 33);
        for (std::size_t i = 1; i < profile.size(); ++i) {
            CHECK(profile[i] >= profile[i - 1]); // monotone
        }
    }
    // Formes caractéristiques au milieu de l'axe.
    CHECK(std::abs(fadeCurveValue(FadeCurve::Linear, 0.5) - 0.5) < 1e-12);
    CHECK(std::abs(fadeCurveValue(FadeCurve::Smooth, 0.5) - 0.5) < 1e-12);
    CHECK(fadeCurveValue(FadeCurve::EaseIn, 0.5) < 0.5);  // départ lent
    CHECK(fadeCurveValue(FadeCurve::EaseOut, 0.5) > 0.5); // départ rapide
}

TEST_CASE("changing the fade curve is one undo step and is read back",
          "[desktop_v2][fade][curve]") {
    Fixture f;
    CHECK_FALSE(setFadeCurve(f.session, f.id, FadeCurve::Steep).ok); // pas de fondu
    f.makeFade();
    const auto before = fadeAxisView(f.session.project(), f.id);
    REQUIRE(before.has_value());
    CHECK(before->curve == FadeCurve::Linear);

    const auto depth = f.session.undoNames().size();
    REQUIRE(setFadeCurve(f.session, f.id, FadeCurve::Steep).ok);
    CHECK(f.session.undoNames().size() == depth + 1);
    const auto after = fadeAxisView(f.session.project(), f.id);
    REQUIRE(after.has_value());
    CHECK(after->curve == FadeCurve::Steep);
    CHECK(fadeSummary(*after).find("Resserrée") != std::string::npos);
    // La même courbe encore une fois ne crée aucun pas d'annulation.
    REQUIRE(setFadeCurve(f.session, f.id, FadeCurve::Steep).ok);
    CHECK(f.session.undoNames().size() == depth + 1);
    // L'axe n'a pas bougé en changeant la courbe.
    CHECK(std::abs(after->start.x - before->start.x) < 0.01);
    CHECK(std::abs(after->end.x - before->end.x) < 0.01);
    REQUIRE(f.session.undo());
    CHECK(fadeAxisView(f.session.project(), f.id)->curve == FadeCurve::Linear);
}
