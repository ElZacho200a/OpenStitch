// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

TEST_CASE("execute / undo / redo sur la pile d'operations") {
    document::Project project;
    UndoStack stack;

    stack.execute(std::make_unique<AppendImageOpCommand>(image::GrayscaleOp{}), project);
    stack.execute(std::make_unique<AppendImageOpCommand>(image::FlipOp{true}), project);
    CHECK(project.ops.size() == 2);
    CHECK(stack.canUndo());
    CHECK_FALSE(stack.canRedo());
    CHECK(stack.undoName() == "Symétrie horizontale");

    CHECK(stack.undo(project));
    CHECK(project.ops.size() == 1);
    CHECK(stack.canRedo());
    CHECK(stack.redoName() == "Symétrie horizontale");

    CHECK(stack.redo(project));
    CHECK(project.ops.size() == 2);

    // undo total = etat initial
    CHECK(stack.undo(project));
    CHECK(stack.undo(project));
    CHECK(project.ops.empty());
    CHECK_FALSE(stack.undo(project));
}

TEST_CASE("une nouvelle commande invalide la branche redo") {
    document::Project project;
    UndoStack stack;

    stack.execute(std::make_unique<AppendImageOpCommand>(image::GrayscaleOp{}), project);
    stack.execute(std::make_unique<AppendImageOpCommand>(image::FlipOp{true}), project);
    stack.undo(project);
    CHECK(stack.canRedo());

    stack.execute(std::make_unique<AppendImageOpCommand>(image::QuantizeOp{4}), project);
    CHECK_FALSE(stack.canRedo());
    CHECK(project.ops.size() == 2);
    CHECK(std::holds_alternative<image::QuantizeOp>(project.ops.back()));
}
