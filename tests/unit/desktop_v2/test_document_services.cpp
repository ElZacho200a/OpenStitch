// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "document_services.hpp"
#include "edit_batch.hpp"
#include "inspector_model.hpp"
#include "object_labels.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "param_spec.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
DocumentSession blank() {
    DocumentSession s;
    auto p = project_ops::create_empty_project({});
    REQUIRE(p.has_value());
    s.replaceProject(std::move(*p), true);
    return s;
}
std::uint64_t rect(DocumentSession& s, double x) {
    return *createdObjectId(s.applyBatch(batchAddRect({x, 0}, 10, 10), "R").report);
}
} // namespace

TEST_CASE("a preset captures an object's settings and applies them elsewhere in one undo step",
          "[desktop_v2][services]") {
    auto s = blank();
    const auto a = rect(s, -20);
    const auto b = rect(s, 20);
    REQUIRE(s.applyBatch(batchSetParam({a}, "tatami", "row_spacing_mm", 0.9), "Écart").committed);
    REQUIRE(savePreset(s, "  Grosse trame  ", a).ok);
    REQUIRE(presetNames(s.project()) == std::vector<std::string>{"Grosse trame"}); // nom nettoyé
    const auto depth = s.undoNames().size();
    REQUIRE(applyPreset(s, 0, {b}).ok);
    CHECK(s.undoNames().size() == depth + 1);
    const auto* ob = &s.project().embroidery_objects[1];
    CHECK(*readParam(*ob, "row_spacing_mm") == Catch::Approx(0.9).margin(0.001));
    REQUIRE(s.undo());
    CHECK(*readParam(s.project().embroidery_objects[1], "row_spacing_mm") ==
          Catch::Approx(0.4).margin(0.001));
    // Même nom : remplace au lieu de dupliquer.
    REQUIRE(savePreset(s, "Grosse trame", b).ok);
    CHECK(s.project().stitch_presets.size() == 1);
    REQUIRE(removePreset(s, 0).ok);
    CHECK(s.project().stitch_presets.empty());
    REQUIRE(s.undo()); // la suppression s'annule
    CHECK(s.project().stitch_presets.size() == 1);
}

TEST_CASE("preset requests that cannot work are refused with a reason", "[desktop_v2][services]") {
    auto s = blank();
    const auto a = rect(s, 0);
    CHECK_FALSE(savePreset(s, "   ", a).ok);
    CHECK_FALSE(savePreset(s, std::string(65, 'x'), a).ok);
    CHECK_FALSE(savePreset(s, "ok", 987654).ok);
    CHECK_FALSE(applyPreset(s, 0, {a}).ok); // aucun préréglage
    REQUIRE(savePreset(s, "ok", a).ok);
    CHECK_FALSE(applyPreset(s, 5, {a}).ok);
    CHECK_FALSE(applyPreset(s, 0, {}).ok);
    CHECK_FALSE(applyPreset(s, 0, {424242}).ok); // objet inexistant
    CHECK_FALSE(removePreset(s, 3).ok);
    for (const auto& r : {savePreset(s, "", a), applyPreset(s, 9, {a})}) {
        CHECK_FALSE(r.message.empty());
    }
}

TEST_CASE("calques: add, rename, hide, lock, reorder, assign and remove",
          "[desktop_v2][services]") {
    auto s = blank();
    CHECK(calques(s.project()).empty()); // un seul calque implicite
    REQUIRE(addCalque(s, "Fond").ok);
    auto list = calques(s.project());
    REQUIRE(list.size() == 2); // « Calque 1 » matérialisé + « Fond »
    CHECK(list[1].name == "Fond");
    const auto first = list[0].id;
    const auto second = list[1].id;
    CHECK(first != second);

    REQUIRE(renameCalque(s, second, "Arrière-plan").ok);
    REQUIRE(setCalqueVisible(s, second, false).ok);
    REQUIRE(setCalqueLocked(s, second, true).ok);
    list = calques(s.project());
    CHECK(list[1].name == "Arrière-plan");
    CHECK_FALSE(list[1].visible);
    CHECK(list[1].locked);

    REQUIRE(moveCalque(s, second, -1).ok);
    CHECK(calques(s.project())[0].id == second);
    CHECK_FALSE(moveCalque(s, second, -1).ok); // déjà au fond

    const auto obj = rect(s, 0);
    REQUIRE(moveObjectsToCalque(s, {obj}, first).ok);
    const auto* o = &s.project().embroidery_objects.front();
    const auto* shape = s.project().findObject(o->source_vector);
    REQUIRE(shape != nullptr);
    CHECK(shape->calque.value == first);

    REQUIRE(removeCalque(s, second).ok);
    CHECK(calques(s.project()).size() == 1);
    CHECK_FALSE(removeCalque(s, first).ok); // le dernier reste
    REQUIRE(s.undo());                      // la suppression s'annule
    CHECK(calques(s.project()).size() == 2);
}

TEST_CASE("calque requests with unknown ids or empty names are refused", "[desktop_v2][services]") {
    auto s = blank();
    CHECK_FALSE(addCalque(s, "  ").ok);
    CHECK_FALSE(renameCalque(s, 123, "x").ok);
    CHECK_FALSE(setCalqueVisible(s, 123, true).ok);
    CHECK_FALSE(setCalqueLocked(s, 123, true).ok);
    CHECK_FALSE(moveCalque(s, 123, 1).ok);
    CHECK_FALSE(removeCalque(s, 123).ok);
    CHECK_FALSE(moveObjectsToCalque(s, {1}, 123).ok);
    REQUIRE(addCalque(s, "A").ok);
    const auto id = calques(s.project())[1].id;
    CHECK_FALSE(renameCalque(s, id, "").ok);
    CHECK_FALSE(moveObjectsToCalque(s, {}, id).ok);
}
