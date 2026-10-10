// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <variant>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

ObjectId add_tatami(document::Project& project, UndoStack& stack) {
    document::EmbroideryObject e;
    e.id = project.object_ids.next();
    document::TatamiParams tp;
    tp.row_spacing = Micrometers{400};
    e.params = tp;
    stack.execute(std::make_unique<AddEmbroideryObjectCommand>(e), project);
    return e.id;
}

Micrometers spacing(const document::Project& project, ObjectId id) {
    return std::get<document::TatamiParams>(project.findEmbroidery(id)->params).row_spacing;
}

document::TatamiParams with_spacing(int um) {
    document::TatamiParams tp;
    tp.row_spacing = Micrometers{um};
    return tp;
}

} // namespace

TEST_CASE("coalescence : rafale sur le meme champ = un seul pas d'annulation") {
    document::Project project;
    UndoStack stack;
    stack.setMergeWindow(std::chrono::hours(1));
    const ObjectId id = add_tatami(project, stack);

    for (int um : {500, 600, 700}) {
        stack.execute(std::make_unique<SetStitchParamsCommand>(id, with_spacing(um),
                                                               "Espacement des rangées"),
                      project);
    }
    CHECK(spacing(project, id) == Micrometers{700});
    CHECK(stack.undoNames().size() == 2); // Ajout + une seule modification
    CHECK(stack.undoName() == "Modifier : Espacement des rangées");

    CHECK(stack.undo(project));
    CHECK(spacing(project, id) == Micrometers{400}); // etat d'avant la rafale
    CHECK(stack.redo(project));
    CHECK(spacing(project, id) == Micrometers{700});
}

TEST_CASE("coalescence : champs differents, objets differents ou sans cle ne fusionnent pas") {
    document::Project project;
    UndoStack stack;
    stack.setMergeWindow(std::chrono::hours(1));
    const ObjectId a = add_tatami(project, stack);
    const ObjectId b = add_tatami(project, stack);

    stack.execute(std::make_unique<SetStitchParamsCommand>(a, with_spacing(500), "Espacement"),
                  project);
    stack.execute(std::make_unique<SetStitchParamsCommand>(a, with_spacing(600), "Angle"), project);
    stack.execute(std::make_unique<SetStitchParamsCommand>(b, with_spacing(600), "Angle"), project);
    stack.execute(std::make_unique<SetStitchParamsCommand>(b, with_spacing(700)), project);
    stack.execute(std::make_unique<SetStitchParamsCommand>(b, with_spacing(800)), project);
    CHECK(stack.undoNames().size() == 2 + 5);
}

TEST_CASE("coalescence : fenetre nulle, undo et coupure de chaine empechent la fusion") {
    document::Project project;
    UndoStack stack;
    const ObjectId id = add_tatami(project, stack);

    stack.setMergeWindow(std::chrono::milliseconds(0));
    stack.execute(std::make_unique<SetStitchParamsCommand>(id, with_spacing(500), "E"), project);
    stack.execute(std::make_unique<SetStitchParamsCommand>(id, with_spacing(600), "E"), project);
    CHECK(stack.undoNames().size() == 3);

    stack.setMergeWindow(std::chrono::hours(1));
    stack.breakMergeChain();
    stack.execute(std::make_unique<SetStitchParamsCommand>(id, with_spacing(700), "E"), project);
    stack.breakMergeChain();
    stack.execute(std::make_unique<SetStitchParamsCommand>(id, with_spacing(800), "E"), project);
    CHECK(stack.undoNames().size() == 5);

    REQUIRE(stack.undo(project)); // 800
    stack.execute(std::make_unique<SetStitchParamsCommand>(id, with_spacing(900), "E"), project);
    CHECK_FALSE(stack.canRedo());
    CHECK(stack.undoNames().size() == 5); // pas de fusion avec le pas precedent un undo
    CHECK(stack.undo(project));
    CHECK(spacing(project, id) == Micrometers{700});
}

TEST_CASE("coalescence : pas aux fleches (translation) sommes, undo exact") {
    document::Project project;
    UndoStack stack;
    stack.setMergeWindow(std::chrono::hours(1));
    document::VectorObject object;
    object.id = project.object_ids.next();
    stack.execute(std::make_unique<AddVectorObjectCommand>(object), project);

    for (int i = 0; i < 3; ++i) {
        auto cmd = std::make_unique<TranslateVectorObjectCommand>(
            object.id, Vec2um{Micrometers{100}, Micrometers{0}});
        cmd->setCoalescable(true);
        stack.execute(std::move(cmd), project);
    }
    CHECK(stack.undoNames().size() == 2);
    // Un glisser souris (non coalescable) n'est jamais absorbé.
    stack.execute(std::make_unique<TranslateVectorObjectCommand>(
                      object.id, Vec2um{Micrometers{5}, Micrometers{5}}),
                  project);
    CHECK(stack.undoNames().size() == 3);
}

TEST_CASE("coalescence : composite de translations fusionne sous-commande par sous-commande") {
    document::Project project;
    UndoStack stack;
    stack.setMergeWindow(std::chrono::hours(1));
    std::vector<ObjectId> ids;
    for (int i = 0; i < 2; ++i) {
        document::VectorObject object;
        object.id = project.object_ids.next();
        ids.push_back(object.id);
        stack.execute(std::make_unique<AddVectorObjectCommand>(object), project);
    }
    const auto step = [&] {
        auto composite = std::make_unique<CompositeCommand>("Deplacer 2 objets");
        std::string key = "translate-set";
        for (const ObjectId id : ids) {
            auto cmd = std::make_unique<TranslateVectorObjectCommand>(
                id, Vec2um{Micrometers{10}, Micrometers{0}});
            cmd->setCoalescable(true);
            key += ":" + std::to_string(id.value);
            composite->add(std::move(cmd));
        }
        composite->setMergeKey(key);
        stack.execute(std::move(composite), project);
    };
    step();
    step();
    step();
    CHECK(stack.undoNames().size() == 3); // 2 ajouts + 1 composite
    CHECK(stack.undoName() == "Deplacer 2 objets");
    CHECK(stack.undo(project));
    CHECK(stack.undoNames().size() == 2);
}

TEST_CASE("historique : undoNames et redoNames ordonnent les pas") {
    document::Project project;
    UndoStack stack;
    stack.setMergeWindow(std::chrono::milliseconds(0));
    stack.execute(std::make_unique<AppendImageOpCommand>(image::GrayscaleOp{}), project);
    stack.execute(std::make_unique<AppendImageOpCommand>(image::FlipOp{true}), project);
    REQUIRE(stack.undo(project));
    CHECK(stack.undoNames().size() == 1);
    REQUIRE(stack.redoNames().size() == 1);
    CHECK(stack.redoNames().front() == stack.redoName());
}
