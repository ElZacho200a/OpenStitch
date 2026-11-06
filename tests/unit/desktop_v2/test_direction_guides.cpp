// SPDX-License-Identifier: Apache-2.0
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

TEST_CASE("every fill type of the list can be applied, listed and undone", "[desktop_v2][types]") {
    Fixture f;
    const auto depth = f.session.undoNames().size();
    for (const auto& t : settableStitchTypes()) {
        if (t.id == "auto_satin" || t.id == "tatami") {
            continue; // le satin a son test dédié ; l'objet est déjà tatami
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
