// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "digitize_pipeline.hpp"
#include "direction_guides.hpp"
#include "document_session.hpp"
#include "inspector_model.hpp"
#include "object_labels.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    DocumentSession session;
    std::uint64_t tatami{0};
    Fixture() {
        DigitizeRequest request;
        request.image = fs::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
        request.target_height_mm = 60.0;
        request.settings.max_colors = 4;
        auto out = run_digitize(request, TaskContext{});
        REQUIRE(out.has_value());
        session.replaceProject(std::move(out->project), true);
        for (const auto& e : session.project().embroidery_objects) {
            if (e.is_tatami()) {
                tatami = e.id.value;
                break;
            }
        }
        REQUIRE(tatami != 0);
    }
    [[nodiscard]] std::string typeOf(std::uint64_t id) const {
        return std::string(stitchTypeId(*session.project().findEmbroidery(ObjectId{id})));
    }
};

} // namespace

TEST_CASE("a direction guide turns a fill directional in one undo step", "[desktop_v2][guides]") {
    Fixture f;
    CHECK(directionGuideCount(f.session.project(), f.tatami) == -1); // pas encore directionnel
    const auto depth = f.session.undoNames().size();
    REQUIRE(addDirectionGuide(f.session, f.tatami, {{-5, -5}, {0, 2}, {6, 4}}, false).ok);
    CHECK(f.session.undoNames().size() == depth + 1);
    CHECK(f.typeOf(f.tatami) == "directional");
    CHECK(directionGuideCount(f.session.project(), f.tatami) >= 1);

    // Une ligne de rupture s'ajoute au même objet, sans reconvertir.
    REQUIRE(addDirectionGuide(f.session, f.tatami, {{-8, 0}, {8, 0}}, true).ok);
    CHECK(f.session.undoNames().size() == depth + 2);
    CHECK(breakLineCount(f.session.project(), f.tatami) == 1);

    // Effacer : une étape chacun ; refus clair quand il n'y a plus rien.
    REQUIRE(clearDirectionGuides(f.session, f.tatami, true).ok);
    CHECK(breakLineCount(f.session.project(), f.tatami) == 0);
    CHECK_FALSE(clearDirectionGuides(f.session, f.tatami, true).ok);
    REQUIRE(f.session.undo()); // rétablit la ligne de rupture
    CHECK(breakLineCount(f.session.project(), f.tatami) == 1);
    REQUIRE(f.session.undo()); // retire la ligne de rupture
    REQUIRE(f.session.undo()); // retire le guide ET rend le tatami d'origine
    CHECK(f.typeOf(f.tatami) == "tatami");
    CHECK(f.session.undoNames().size() == depth);
}

TEST_CASE("guides refuse bad input and leave the document untouched", "[desktop_v2][guides]") {
    Fixture f;
    const auto depth = f.session.undoNames().size();
    CHECK_FALSE(addDirectionGuide(f.session, f.tatami, {{1, 1}}, false).ok);            // 1 point
    CHECK_FALSE(addDirectionGuide(f.session, f.tatami, {{1, 1}, {1.05, 1}}, false).ok); // confondus
    CHECK_FALSE(
        addDirectionGuide(f.session, f.tatami, {{0, 0}, {5000, 0}}, false).ok);    // hors motif
    CHECK_FALSE(addDirectionGuide(f.session, 999999, {{0, 0}, {5, 5}}, false).ok); // inconnu
    CHECK_FALSE(clearDirectionGuides(f.session, f.tatami, false).ok); // pas directionnel
    CHECK(f.session.undoNames().size() == depth);
    CHECK(f.typeOf(f.tatami) == "tatami");
}

TEST_CASE("fill guides are listed, reshaped and removed one by one", "[desktop_v2][guides]") {
    Fixture f;
    CHECK(canDirectionGuide(f.session.project(), f.tatami));
    CHECK_FALSE(canDirectionGuide(f.session.project(), 999999));
    CHECK(fillGuideCurves(f.session.project(), f.tatami, false).empty()); // pas directionnel

    REQUIRE(addDirectionGuide(f.session, f.tatami, {{-5, -5}, {0, 2}, {6, 4}}, false).ok);
    const auto curves = fillGuideCurves(f.session.project(), f.tatami, false);
    const auto count = directionGuideCount(f.session.project(), f.tatami);
    REQUIRE(static_cast<int>(curves.size()) == count);
    const auto last = static_cast<std::size_t>(count - 1);
    REQUIRE(curves[last].nodes.size() >= 2);
    REQUIRE(curves[last].curve.size() >= 2);
    CHECK(curves[last].nodes.front().x == Catch::Approx(-5.0).margin(0.001));
    CHECK(curves[last].nodes.front().y == Catch::Approx(-5.0).margin(0.001));

    // Désignation : un point du tracé désigne le guide ; loin de tout, rien.
    const auto mid = curves[last].curve[curves[last].curve.size() / 2];
    CHECK(fillGuideAt(f.session.project(), f.tatami, false, mid, 0.5) >= 0);
    CHECK(fillGuideAt(f.session.project(), f.tatami, false, {900, 900}, 0.5) == -1);
    CHECK(fillGuideAt(f.session.project(), f.tatami, true, mid, 0.5) == -1); // aucune rupture

    // Déplacer un nœud : une étape, la courbe suit, l'annulation rend l'état exact.
    const auto depth = f.session.undoNames().size();
    const auto paramsOf = [&f] {
        return std::get<document::DirectionalFillParams>(
            f.session.project().findEmbroidery(ObjectId{f.tatami})->params);
    };
    const auto before = paramsOf();
    REQUIRE(moveFillGuideNode(f.session, f.tatami, false, last, 0, {-5, -2}).ok);
    CHECK(f.session.undoNames().size() == depth + 1);
    const auto moved = fillGuideCurves(f.session.project(), f.tatami, false)[last];
    CHECK(moved.nodes.front().y == Catch::Approx(-2.0).margin(0.001));
    CHECK(moved.curve.front().y == Catch::Approx(-2.0).margin(0.001));
    REQUIRE(f.session.undo());
    CHECK(paramsOf() == before);

    // Refus sans mutation : indices inconnus, position hors motif, nœuds confondus.
    CHECK_FALSE(moveFillGuideNode(f.session, f.tatami, false, 99, 0, {0, 0}).ok);
    CHECK_FALSE(moveFillGuideNode(f.session, f.tatami, false, last, 99, {0, 0}).ok);
    CHECK_FALSE(moveFillGuideNode(f.session, f.tatami, false, last, 0, {5000, 0}).ok);
    const auto next = curves[last].nodes[1];
    CHECK_FALSE(moveFillGuideNode(f.session, f.tatami, false, last, 0, next).ok);
    CHECK_FALSE(removeFillGuide(f.session, f.tatami, true, 0).ok); // aucune rupture
    CHECK(f.session.undoNames().size() == depth);

    // Retirer UN guide : une étape, les autres restent.
    REQUIRE(removeFillGuide(f.session, f.tatami, false, last).ok);
    CHECK(directionGuideCount(f.session.project(), f.tatami) == count - 1);
    CHECK(f.session.undoNames().size() == depth + 1);
    REQUIRE(f.session.undo());
    CHECK(directionGuideCount(f.session.project(), f.tatami) == count);

    // Les ruptures se gèrent de la même façon.
    REQUIRE(addDirectionGuide(f.session, f.tatami, {{-8, 0}, {8, 0}}, true).ok);
    const auto breaks = fillGuideCurves(f.session.project(), f.tatami, true);
    REQUIRE(breaks.size() == 1);
    REQUIRE(moveFillGuideNode(f.session, f.tatami, true, 0, 1, {8, 3}).ok);
    CHECK(fillGuideCurves(f.session.project(), f.tatami, true)[0].nodes[1].y ==
          Catch::Approx(3.0).margin(0.001));
    REQUIRE(removeFillGuide(f.session, f.tatami, true, 0).ok);
    CHECK(breakLineCount(f.session.project(), f.tatami) == 0);
}

TEST_CASE("auto guides convert a fill in one undo step and keep break lines",
          "[desktop_v2][guides]") {
    Fixture f;
    const auto depth = f.session.undoNames().size();
    REQUIRE(autoDirectionGuides(f.session, f.tatami).ok);
    CHECK(f.session.undoNames().size() == depth + 1);
    CHECK(f.typeOf(f.tatami) == "directional");
    const auto first = fillGuideCurves(f.session.project(), f.tatami, false);
    REQUIRE_FALSE(first.empty());

    // Les ruptures sont conservées quand les guides sont régénérés.
    REQUIRE(addDirectionGuide(f.session, f.tatami, {{-8, 0}, {8, 0}}, true).ok);
    REQUIRE(addDirectionGuide(f.session, f.tatami, {{-3, -3}, {3, 3}}, false).ok);
    REQUIRE(autoDirectionGuides(f.session, f.tatami).ok);
    CHECK(breakLineCount(f.session.project(), f.tatami) == 1);
    const auto again = fillGuideCurves(f.session.project(), f.tatami, false);
    REQUIRE(again.size() == first.size());
    for (std::size_t i = 0; i < again.size(); ++i) {
        CHECK(again[i].nodes.size() == first[i].nodes.size());
    }

    // Une seule annulation par action : on revient au tatami d'origine.
    while (f.session.undoNames().size() > depth) {
        REQUIRE(f.session.undo());
    }
    CHECK(f.typeOf(f.tatami) == "tatami");
    CHECK_FALSE(autoDirectionGuides(f.session, 999999).ok);
}

TEST_CASE("every fill type of the list can be applied, listed and undone", "[desktop_v2][types]") {
    Fixture f;
    const auto depth = f.session.undoNames().size();
    for (const auto& t : settableStitchTypes()) {
        if (t.id == "auto_satin" || t.id == "tatami") {
            continue; // le satin a son test dédié ; l'objet est déjà tatami
        }
        if (t.id == "multicolor_gradient") {
            continue; // creation composite dediee (InspectorController::setStitchType), pas le
                      // batch generique set_stitch_type -- comme auto_satin ci-dessus
        }
        INFO("type " << t.id);
        const auto applied =
            f.session.applyBatch(batchSetStitchType({f.tatami}, t.id), std::string(t.label));
        REQUIRE(applied.committed);
        // L'identifiant affiché par l'objet retombe sur celui de la liste déroulante.
        CHECK(settableIdOf(f.typeOf(f.tatami)) == t.id);
        REQUIRE(f.session.undo());
        CHECK(f.typeOf(f.tatami) == "tatami");
    }
    CHECK(f.session.undoNames().size() == depth);
    // Les six remplissages ajoutés par rapport à l'ancienne liste y sont bien.
    for (const char* id :
         {"contour_fill", "radial_fill", "crosshatch_fill", "motif_fill", "motif_run", "blanket"}) {
        CHECK(std::any_of(settableStitchTypes().begin(), settableStitchTypes().end(),
                          [id](const auto& t) { return t.id == id; }));
    }
}
