// SPDX-License-Identifier: Apache-2.0
// Scénarios de recette de la mission « finalisation V2 », au niveau du cœur (sans fenêtre) : chaque
// scénario enchaîne les services comme le fait l'interface, puis vérifie le fichier machine
// produit.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

#include "digitize_pipeline.hpp"
#include "document_session.hpp"
#include "fade_axis.hpp"
#include "fade_service.hpp"
#include "node_edit.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "param_spec.hpp"
#include "path_optimizer.hpp"
#include "satin_column.hpp"
#include "shape_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

DocumentSession blankSession() {
    DocumentSession s;
    auto created = project_ops::create_empty_project({});
    REQUIRE(created.has_value());
    s.replaceProject(std::move(*created), true);
    return s;
}

std::filesystem::path tempDst(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

// Écrit le DST de la séquence EFFECTIVE puis le relit : preuve que le fichier est exploitable.
stitch::StitchStats exportAndReadBack(const DocumentSession& session, const char* name) {
    const auto path = tempDst(name);
    std::filesystem::remove(path);
    TaskContext task;
    auto stats = export_project_dst(session.snapshot(), path, task);
    REQUIRE(stats.has_value());
    REQUIRE(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path) > 512); // entête DST + au moins des points
    const auto back = formats::read_dst_file(path);
    REQUIRE(back.has_value());
    CHECK(stitch::compute_stats(*back).stitches > 0);
    std::filesystem::remove(path);
    return *stats;
}

} // namespace

TEST_CASE("scenario 2: draw, edit nodes, build a satin, edit its rails, set the underlay, export",
          "[desktop_v2][scenario]") {
    auto s = blankSession();

    // Une forme dessinée, ses nœuds modifiés, le tout annulable pas à pas.
    REQUIRE(s.applyBatch(batchAddRect({0, 0}, 30, 20), "Rectangle").committed);
    const auto shapeId = s.project().embroidery_objects.front().id.value;
    const auto view = nodeView(s.project(), shapeId);
    REQUIRE(view.has_value());
    REQUIRE(moveNodes(s, shapeId, {view->nodes.front().ref}, {1.0, 0.5}, false).ok);

    // Une colonne satin à trois barreaux, ses rails retouchés, sa sous-couche réglée.
    const auto satin =
        createSatinColumn(s, {{{-20, 30}, {-20, 36}}, {{0, 30}, {0, 36}}, {{20, 30}, {20, 36}}});
    REQUIRE(satin.ok);
    const auto rails = nodeView(s.project(), satin.objectId);
    REQUIRE(rails.has_value());
    CHECK(rails->satin);
    const auto depth = s.undoNames().size();
    REQUIRE(moveNodes(s, satin.objectId, {NodeRef{0, 0, 1}}, {0.0, 1.0}, false).ok);
    CHECK(s.undoNames().size() == depth + 1);
    const auto underlay = batchSetParam({satin.objectId}, "satin", "center_underlay", 0.0);
    REQUIRE_FALSE(underlay.empty());
    REQUIRE(s.applyBatch(underlay, "Sous-couche").committed);
    const auto* object = s.project().findEmbroidery(ObjectId{satin.objectId});
    REQUIRE(object != nullptr);
    const auto* params = std::get_if<document::SatinParams>(&object->params);
    REQUIRE(params != nullptr);
    CHECK_FALSE(params->center_underlay);

    // Export : un fichier machine relisible, avec des points.
    const auto stats = exportAndReadBack(s, "os-scenario2.dst");
    CHECK(stats.stitches > 100);

    // Tout est annulable jusqu'au document vide.
    while (s.canUndo()) {
        REQUIRE(s.undo());
    }
    CHECK(s.project().embroidery_objects.empty());
}

TEST_CASE("scenario 3: several objects, a fade, a shrinkage-aware order, then export",
          "[desktop_v2][scenario]") {
    auto s = blankSession();
    const char* colours[] = {"#FF0000", "#0000FF", "#FF0000", "#0000FF"};
    const double xs[] = {-45.0, 15.0, -15.0, 45.0};
    for (int i = 0; i < 4; ++i) {
        REQUIRE(s.applyBatch(batchAddRect({xs[i], 0}, 20, 20), "Carré").committed);
    }
    for (int i = 0; i < 4; ++i) {
        const auto id = s.project().embroidery_objects[static_cast<std::size_t>(i)].id.value;
        const std::string batch =
            "{\"schema\":\"openstitch.batch/1\",\"operations\":[{\"op\":\"set_color\",\"object\":" +
            std::to_string(id) + ",\"rgb\":\"" + colours[i] + "\"}]}";
        REQUIRE(s.applyBatch(batch, "Couleur").committed);
    }

    // Un fondu sur le premier carré, son axe déplacé (une étape d'annulation chacun).
    const auto first = s.project().embroidery_objects.front().id.value;
    REQUIRE(createFade(s, first, FadeSettings{{20, 160, 60}, true}).ok);
    const auto axis = fadeAxisView(s.project(), first);
    REQUIRE(axis.has_value());
    const auto depth = s.undoNames().size();
    REQUIRE(dragFadeHandle(s, first, FadeHandle::Move, {axis->centre.x + 2.0, axis->centre.y + 1.0},
                           false)
                .ok);
    CHECK(s.undoNames().size() == depth + 1);

    // L'ordre est optimisé avec des mesures réelles ; accepter ne dégrade pas les changements de
    // fil.
    TaskContext task;
    const auto proposal =
        proposeOrder(s.snapshot(), s.revision(), PathStrategy::ColorThenProximity, true, task);
    REQUIRE(proposal.has_value());
    if (proposal->changes) {
        CHECK(proposal->after.colorChanges <= proposal->before.colorChanges);
        REQUIRE(applyProposal(s, *proposal).ok);
    }
    const auto stats = exportAndReadBack(s, "os-scenario3.dst");
    CHECK(stats.stitches > 100);
    if (proposal->changes) {
        CHECK(stats.color_changes == proposal->after.colorChanges);
    }
}

TEST_CASE("scenario 5: a task result for an older document never replaces the current one",
          "[desktop_v2][scenario]") {
    auto s = blankSession();
    REQUIRE(s.applyBatch(batchAddRect({0, 0}, 20, 20), "Carré").committed);
    TaskContext task;
    const auto stale = proposeOrder(s.snapshot(), s.revision(), PathStrategy::ByColor, false, task);
    // Un second carré est ajouté pendant que « la tâche » calculait.
    REQUIRE(s.applyBatch(batchAddRect({40, 0}, 20, 20), "Autre").committed);
    if (stale.has_value()) {
        CHECK_FALSE(applyProposal(s, *stale).ok); // périmé : refusé, document intact
    }
    CHECK(s.project().embroidery_objects.size() == 2);
    // Remplacer le projet (changement de document) repart d'un historique vierge.
    auto fresh = project_ops::create_empty_project({});
    REQUIRE(fresh.has_value());
    s.replaceProject(std::move(*fresh), true);
    CHECK_FALSE(s.canUndo());
    CHECK_FALSE(s.dirty());
}
