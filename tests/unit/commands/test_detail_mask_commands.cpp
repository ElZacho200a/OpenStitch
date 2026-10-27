// SPDX-License-Identifier: Apache-2.0
//
// Zones « détail / aplat » : traits de pinceau annulables à l'identique.
#include <catch2/catch_test_macros.hpp>

#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

TEST_CASE("pinceau de detail : peindre puis annuler retire le masque cree") {
    document::Project project;
    UndoStack stack;
    stack.execute(
        std::make_unique<PaintDetailMaskCommand>(
            20, 10, std::vector<std::pair<int, int>>{{5, 5}, {7, 5}, {9, 5}}, 2, false, 1.0),
        project);
    REQUIRE(project.detail_mask.any_detail());
    CHECK(project.detail_mask.matches(20, 10));

    stack.undo(project);
    CHECK(project.detail_mask.empty());
    stack.redo(project);
    CHECK(project.detail_mask.any_detail());
}

TEST_CASE("pinceau de detail : gomme puis annuler rend les pixels effaces") {
    document::Project project;
    UndoStack stack;
    stack.execute(std::make_unique<PaintDetailMaskCommand>(
                      20, 10, std::vector<std::pair<int, int>>{{10, 5}}, 4, false, 1.0),
                  project);
    const auto painted = project.detail_mask;

    // Deux disques qui se recouvrent : l'annulation doit restituer chaque pixel une seule fois.
    stack.execute(
        std::make_unique<PaintDetailMaskCommand>(
            20, 10, std::vector<std::pair<int, int>>{{9, 5}, {10, 5}, {11, 5}}, 2, true, 1.0),
        project);
    CHECK(project.detail_mask.weights[5 * 20 + 10] == 0);
    CHECK(project.detail_mask != painted);

    stack.undo(project);
    CHECK(project.detail_mask == painted);
}

TEST_CASE("pinceau de detail : tout effacer est annulable") {
    document::Project project;
    UndoStack stack;
    stack.execute(std::make_unique<PaintDetailMaskCommand>(
                      20, 10, std::vector<std::pair<int, int>>{{10, 5}}, 3, false, 1.0),
                  project);
    const auto painted = project.detail_mask;
    stack.execute(std::make_unique<ClearDetailMaskCommand>(), project);
    CHECK(project.detail_mask.empty());
    stack.undo(project);
    CHECK(project.detail_mask == painted);
}
