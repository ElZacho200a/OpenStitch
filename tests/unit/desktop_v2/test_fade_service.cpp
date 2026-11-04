// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "edit_batch.hpp"
#include "fade_service.hpp"
#include "inspector_model.hpp"
#include "openstitch/project_ops/factory.hpp"

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
