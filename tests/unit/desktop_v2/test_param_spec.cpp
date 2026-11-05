// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "document_session.hpp"
#include "edit_batch.hpp"
#include "inspector_model.hpp"
#include "object_labels.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "param_spec.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
struct Doc {
    DocumentSession session;
    std::uint64_t id{0};
};
Doc rectWithType(std::string_view type) {
    Doc d;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    d.session.replaceProject(std::move(*created), true);
    const auto r = d.session.applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle");
    REQUIRE(r.committed);
    d.id = *createdObjectId(r.report);
    if (type != "tatami") {
        REQUIRE(d.session.applyBatch(batchSetStitchType({d.id}, type), "Type").committed);
    }
    return d;
}
} // namespace

TEST_CASE("every spec key is readable on its own type and refused on the others",
          "[desktop_v2][params]") {
    for (const std::string_view type :
         {"running", "tatami", "directional", "spiral_fill", "cross_stitch", "applique"}) {
        auto d = rectWithType(type);
        const auto& object = d.session.project().embroidery_objects.front();
        const auto specs = paramSpecs(stitchTypeId(object));
        REQUIRE_FALSE(specs.empty());
        for (const auto& s : specs) {
            INFO(std::string(type) + "." + std::string(s.key));
            const auto v = readParam(object, s.key);
            REQUIRE(v.has_value());
            CHECK(*v >= s.min - 1e-6);
            CHECK(*v <=
                  s.max + 1e-6); // la valeur par défaut du moteur est dans la plage de la table
        }
    }
    auto tatami = rectWithType("tatami");
    CHECK_FALSE(
        readParam(tatami.session.project().embroidery_objects.front(), "repeats").has_value());
    CHECK(paramSpecs("no_such_type").empty());
}

TEST_CASE("setting a parameter is one undo step and reads back in display units",
          "[desktop_v2][params]") {
    auto d = rectWithType("tatami");
    const auto depth = d.session.undoNames().size();
    REQUIRE(d.session.applyBatch(batchSetParam({d.id}, "tatami", "row_spacing_mm", 0.55), "Écart")
                .committed);
    CHECK(d.session.undoNames().size() == depth + 1);
    const auto& o = d.session.project().embroidery_objects.front();
    CHECK(*readParam(o, "row_spacing_mm") == Catch::Approx(0.55).margin(0.001));
    REQUIRE(d.session.applyBatch(batchSetParam({d.id}, "tatami", "angle_deg", 45.0), "Angle")
                .committed);
    CHECK(*readParam(d.session.project().embroidery_objects.front(), "angle_deg") ==
          Catch::Approx(45.0).margin(0.01));
    REQUIRE(d.session.applyBatch(batchSetParam({d.id}, "tatami", "underlay_edge", 1), "Sous-couche")
                .committed);
    CHECK(*readParam(d.session.project().embroidery_objects.front(), "underlay_edge") == 1.0);
    REQUIRE(d.session.undo());
    CHECK(*readParam(d.session.project().embroidery_objects.front(), "underlay_edge") == 0.0);
}

TEST_CASE("integers are rounded and out-of-range or unknown settings are refused",
          "[desktop_v2][params]") {
    auto d = rectWithType("running");
    REQUIRE(d.session.applyBatch(batchSetParam({d.id}, "running", "repeats", 2.4), "Passages")
                .committed);
    CHECK(*readParam(d.session.project().embroidery_objects.front(), "repeats") == 2.0);
    CHECK(batchSetParam({d.id}, "running", "repeats", 7).empty());
    CHECK(batchSetParam({d.id}, "running", "stitch_length_mm", 99).empty());
    CHECK(batchSetParam({d.id}, "running", "row_spacing_mm", 0.4)
              .empty()); // pas un réglage de ce type
    CHECK(batchSetParam({d.id}, "running", "repeats", std::nan("")).empty());
    CHECK(batchSetParam({}, "running", "repeats", 2).empty());
}

TEST_CASE("one change applies to every selected object in a single undo step",
          "[desktop_v2][params]") {
    Doc d;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    d.session.replaceProject(std::move(*created), true);
    const auto a =
        *createdObjectId(d.session.applyBatch(batchAddRect({-20, 0}, 10, 10), "R").report);
    const auto b =
        *createdObjectId(d.session.applyBatch(batchAddRect({20, 0}, 10, 10), "R").report);
    const auto depth = d.session.undoNames().size();
    REQUIRE(
        d.session.applyBatch(batchSetParam({a, b}, "tatami", "stitch_length_mm", 2.5), "Longueur")
            .committed);
    CHECK(d.session.undoNames().size() == depth + 1);
    for (const auto& o : d.session.project().embroidery_objects) {
        CHECK(*readParam(o, "stitch_length_mm") == Catch::Approx(2.5).margin(0.001));
    }
}

TEST_CASE("choice settings write the engine keyword and read back as an index",
          "[desktop_v2][params]") {
    auto d = rectWithType("tatami");
    const auto specs = paramSpecs("tatami");
    const auto pull =
        std::ranges::find_if(specs, [](const auto& s) { return s.key == "pull_mode"; });
    REQUIRE(pull != specs.end());
    CHECK(pull->kind == ParamKind::Choice);
    const auto options = paramOptions(*pull);
    REQUIRE(options.size() == 4);
    CHECK(options[3].first == "predicted"); // compensation de tirage PRÉDICTIVE
    // L'index 3 envoie « predicted » : le moteur l'accepte et la valeur relue est 3.
    REQUIRE(
        d.session.applyBatch(batchSetParam({d.id}, "tatami", "pull_mode", 3), "Tirage").committed);
    CHECK(*readParam(d.session.project().embroidery_objects.front(), "pull_mode") == 3.0);
    REQUIRE(d.session.applyBatch(batchSetParam({d.id}, "tatami", "underlay_mode", 1), "Sous-couche")
                .committed);
    CHECK(*readParam(d.session.project().embroidery_objects.front(), "underlay_mode") == 1.0);
    CHECK(batchSetParam({d.id}, "tatami", "pull_mode", 4).empty()); // hors des options
    CHECK(
        batchSetParam({d.id}, "tatami", "pull_mode", -1).empty()); // « mixte » n'est pas une valeur
}

TEST_CASE("special fills expose their own essential settings", "[desktop_v2][params]") {
    auto cross = rectWithType("cross_stitch");
    REQUIRE(cross.session
                .applyBatch(batchSetParam({cross.id}, "cross_stitch", "cell_mm", 3.5), "Cellule")
                .committed);
    CHECK(*readParam(cross.session.project().embroidery_objects.front(), "cell_mm") ==
          Catch::Approx(3.5).margin(0.001));
    auto appl = rectWithType("applique");
    REQUIRE(
        appl.session
            .applyBatch(batchSetParam({appl.id}, "applique", "cover_width_mm", 4.0), "Couverture")
            .committed);
    CHECK(*readParam(appl.session.project().embroidery_objects.front(), "cover_width_mm") ==
          Catch::Approx(4.0).margin(0.001));
    auto spiral = rectWithType("spiral_fill");
    REQUIRE(spiral.session.applyBatch(batchSetParam({spiral.id}, "spiral", "clockwise", 1), "Sens")
                .committed);
    CHECK(*readParam(spiral.session.project().embroidery_objects.front(), "clockwise") == 1.0);
}

TEST_CASE("settings are split into essentials and advanced ones", "[desktop_v2][params][levels]") {
    for (const char* type : {"tatami", "directional", "satin"}) {
        const auto specs = paramSpecs(type);
        std::size_t essentials = 0;
        std::size_t advanced = 0;
        for (const auto& spec : specs) {
            (paramLevel(spec) == ParamLevel::Essential ? essentials : advanced) += 1;
        }
        INFO(type);
        CHECK(essentials >= 4); // une vue de base lisible
        CHECK(advanced >= 2);   // et des réglages fins à la demande
    }
    // Les réglages que l'utilisateur change tous les jours restent visibles sans effort.
    for (const auto& spec : paramSpecs("tatami")) {
        if (spec.key == "row_spacing_mm" || spec.key == "angle_deg" ||
            spec.key == "pull_compensation_mm") {
            CHECK(paramLevel(spec) == ParamLevel::Essential);
        }
        if (spec.key == "stagger" || spec.key == "underlay_inset_mm") {
            CHECK(paramLevel(spec) == ParamLevel::Advanced);
        }
    }
}

TEST_CASE("search ignores case and accents and looks at the label and the key",
          "[desktop_v2][params][levels]") {
    const auto specs = paramSpecs("tatami");
    const auto find = [&](std::string_view key) {
        for (const auto& s : specs) {
            if (s.key == key) {
                return s;
            }
        }
        FAIL("réglage introuvable : " << key);
        return specs.front();
    };
    CHECK(paramMatches(find("row_spacing_mm"), "ECART")); // « Écart entre rangées »
    CHECK(paramMatches(find("row_spacing_mm"), "écart"));
    CHECK(paramMatches(find("underlay_edge"), "sous-couche")); // libellé
    CHECK(paramMatches(find("underlay_edge"), "underlay"));    // clé
    CHECK(paramMatches(find("angle_deg"), ""));                // requête vide : tout correspond
    CHECK_FALSE(paramMatches(find("angle_deg"), "sous-couche"));
}

TEST_CASE("the new advanced settings are readable, settable within range, and undoable",
          "[desktop_v2][params][levels]") {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    REQUIRE(s.applyBatch(batchAddRect({0, 0}, 20, 10), "Rectangle").committed);
    const auto id = s.project().embroidery_objects.front().id.value;
    const auto& object = s.project().embroidery_objects.front();

    const auto batch = batchSetParam({id}, "tatami", "stagger", 5.0);
    REQUIRE_FALSE(batch.empty());
    REQUIRE(s.applyBatch(batch, "Décalage").committed);
    CHECK(readParam(s.project().embroidery_objects.front(), "stagger") == 5.0);
    CHECK(batchSetParam({id}, "tatami", "stagger", 9.0).empty()); // hors plage [1 ; 8]
    CHECK(batchSetParam({id}, "tatami", "row_end_clip_mm", 2.5) != "");
    REQUIRE(s.undo());
    CHECK(readParam(object, "stagger") != 5.0);
}
