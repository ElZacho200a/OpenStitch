// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "edit_batch.hpp"
#include "fade_service.hpp"
#include "inspector_model.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/project_ops/gradient_fill.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
struct Fixture {
    DocumentSession session;
    std::uint64_t id{0};
};
Fixture rectFill() {
    Fixture f;
    auto p = project_ops::create_empty_project({});
    REQUIRE(p.has_value());
    f.session.replaceProject(std::move(*p), true);
    f.id = *createdObjectId(f.session.applyBatch(batchAddRect({0, 0}, 30, 20), "R").report);
    return f;
}
} // namespace

TEST_CASE("a fade is created, edited and removed, each in one undo step", "[desktop_v2][fade]") {
    auto f = rectFill();
    CHECK(fadeIncompatibility(f.session.project(), f.id).empty());
    CHECK_FALSE(readFade(f.session.project(), f.id).has_value());
    const auto objects = f.session.project().embroidery_objects.size();
    const auto depth = f.session.undoNames().size();

    FadeSettings settings;
    settings.foreground = {250, 120, 10};
    REQUIRE(createFade(f.session, f.id, settings).ok);
    CHECK(f.session.undoNames().size() == depth + 1);               // deux couches, UNE étape
    CHECK(f.session.project().embroidery_objects.size() > objects); // la seconde couche existe
    const auto read = readFade(f.session.project(), f.id);
    REQUIRE(read.has_value());
    CHECK(read->foreground == settings.foreground);
    CHECK(read->complementary);

    settings.complementary = false;
    settings.foreground = {10, 200, 90};
    REQUIRE(editFade(f.session, f.id, settings).ok);
    const auto edited = readFade(f.session.project(), f.id);
    REQUIRE(edited.has_value());
    CHECK(edited->foreground == settings.foreground);
    CHECK_FALSE(edited->complementary);

    REQUIRE(removeFade(f.session, f.id).ok);
    CHECK_FALSE(readFade(f.session.project(), f.id).has_value());
    REQUIRE(f.session.undo()); // la suppression s'annule : le fondu revient
    CHECK(readFade(f.session.project(), f.id).has_value());
}

TEST_CASE("fade requests that cannot work are refused with the engine's reason",
          "[desktop_v2][fade]") {
    auto f = rectFill();
    FadeSettings settings;
    settings.foreground = {1, 2, 3};
    CHECK_FALSE(createFade(f.session, 987654, settings).ok);
    CHECK_FALSE(editFade(f.session, f.id, settings).ok); // pas de fondu à éditer
    CHECK_FALSE(removeFade(f.session, f.id).ok);

    // Un contour n'est pas un remplissage : le moteur dit pourquoi.
    REQUIRE(f.session.applyBatch(batchSetStitchType({f.id}, "running"), "Type").committed);
    const auto why = fadeIncompatibility(f.session.project(), f.id);
    CHECK_FALSE(why.empty());
    const auto refused = createFade(f.session, f.id, settings);
    CHECK_FALSE(refused.ok);
    CHECK(refused.message == why);

    // Même fil que le fond : refusé avant toute commande.
    auto g = rectFill();
    FadeSettings same;
    same.foreground = g.session.project().embroidery_objects.front().rgb;
    const auto depth = g.session.undoNames().size();
    CHECK_FALSE(createFade(g.session, g.id, same).ok);
    CHECK(g.session.undoNames().size() == depth);
}

TEST_CASE("rail-less satin creates an autonomous interlaced gradient",
          "[desktop_v2][fade][satin]") {
    auto f = rectFill();
    auto* object = f.session.projectForIdAllocation().findEmbroidery(ObjectId{f.id});
    REQUIRE(object != nullptr);
    object->params = document::SatinParams{};

    // Régression PR #340 : choisir Satin ne doit ni afficher l'ancien diagnostic « tatami », ni
    // exiger une saisie manuelle préalable. Les rails sont dérivés du contour par le moteur.
    CHECK(fadeIncompatibility(f.session.project(), f.id).empty());
    MultiFadeSettings settings;
    settings.colors = {{255, 0, 0}, {0, 0, 255}};
    settings.method = static_cast<int>(document::GradientMethod::SatinInterlaced);
    const auto depth = f.session.undoNames().size();
    REQUIRE(createMultiFade(f.session, f.id, settings).ok);
    CHECK(f.session.undoNames().size() == depth + 1);

    const auto spec =
        project_ops::describe_multicolor_gradient(f.session.project(), ObjectId{f.id});
    REQUIRE(spec.has_value());
    CHECK(spec->method == document::GradientMethod::SatinInterlaced);
    CHECK(spec->palette.size() == 2);
}

TEST_CASE("autonomous gradient method settings survive desktop edits", "[desktop_v2][fade]") {
    auto f = rectFill();
    MultiFadeSettings settings;
    settings.colors = {{10, 20, 30}, {70, 80, 90}, {120, 130, 140}};
    settings.method = static_cast<int>(document::GradientMethod::CrossHatching);
    settings.crosshatchLayers = 3;
    settings.crosshatchAngleDeg = 37.5;
    REQUIRE(createMultiFade(f.session, f.id, settings).ok);

    const auto read = readMultiFade(f.session.project(), f.id);
    REQUIRE(read.has_value());
    CHECK(read->method == settings.method);
    CHECK(read->crosshatchLayers == 3);
    CHECK(read->crosshatchAngleDeg == Catch::Approx(37.5));

    auto edited = *read;
    edited.method = static_cast<int>(document::GradientMethod::LongAndShort);
    edited.geometry = 2;
    REQUIRE(editMultiFade(f.session, f.id, edited).ok);
    const auto after = readMultiFade(f.session.project(), f.id);
    REQUIRE(after.has_value());
    CHECK(after->method == static_cast<int>(document::GradientMethod::LongAndShort));
    CHECK(after->geometry == 2);
}
