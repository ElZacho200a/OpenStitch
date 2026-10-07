// SPDX-License-Identifier: Apache-2.0
// AD-04 : l'arrivee d'un design importe dans le document passe par ICommand
// (Tier 0) -- remplace l'ancienne exception desktop
// MainWindow::sequenceImported_, qui echappait entierement a l'undo stack.
#include <catch2/catch_test_macros.hpp>

#include "openstitch/commands/import_machine_design_command.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

TEST_CASE("SetImportedDesignCommand : apply puis revert restaurent l'etat exact") {
    document::Project project;
    CHECK_FALSE(project.imported_design.has_value());

    document::ImportedDesign imported;
    imported.source_format = "dst";
    imported.sequence.commands = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, stitch::CommandType::Stitch, ObjectId{}},
    };

    UndoStack stack;
    stack.execute(std::make_unique<SetImportedDesignCommand>(imported), project);
    REQUIRE(project.imported_design.has_value());
    CHECK(*project.imported_design == imported);

    REQUIRE(stack.undo(project));
    CHECK_FALSE(project.imported_design.has_value());

    REQUIRE(stack.redo(project));
    REQUIRE(project.imported_design.has_value());
    CHECK(*project.imported_design == imported);
}

TEST_CASE("SetImportedDesignCommand : remplace un design importe deja present") {
    document::Project project;
    document::ImportedDesign first;
    first.source_format = "dst";
    first.sequence.commands = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, stitch::CommandType::Stitch, ObjectId{}},
    };
    project.imported_design = first;

    document::ImportedDesign second;
    second.source_format = "dst";
    second.sequence.commands = {
        {Vec2um{Micrometers{1'000}, Micrometers{0}}, stitch::CommandType::Stitch, ObjectId{}},
    };

    UndoStack stack;
    stack.execute(std::make_unique<SetImportedDesignCommand>(second), project);
    CHECK(*project.imported_design == second);

    REQUIRE(stack.undo(project));
    REQUIRE(project.imported_design.has_value());
    CHECK(*project.imported_design == first);
}
