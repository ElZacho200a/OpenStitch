// SPDX-License-Identifier: Apache-2.0
//
// HP-OBJ-024 : commandes sur les calques du document (ajout, suppression, renommage,
// visibilité, verrou, déplacement, affectation de formes), toutes annulables.
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/commands/calque_commands.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"

using namespace openstitch;

namespace {

document::Project project_with_two_shapes(ObjectId& a, ObjectId& b) {
    document::Project project;
    document::VectorObject va;
    va.id = project.object_ids.next();
    a = va.id;
    project.vector_objects.push_back(va);
    document::VectorObject vb;
    vb.id = project.object_ids.next();
    b = vb.id;
    project.vector_objects.push_back(vb);
    return project;
}

} // namespace

TEST_CASE("calques : ajouter sur un document sans calque materialise le calque du fond") {
    ObjectId a;
    ObjectId b;
    auto project = project_with_two_shapes(a, b);
    commands::UndoStack stack;
    const ObjectId first = project.object_ids.next();
    const ObjectId second = project.object_ids.next();
    stack.execute(std::make_unique<commands::AddCalqueCommand>(second, "Effets", first), project);
    REQUIRE(project.calques.size() == 2);
    CHECK(project.calques[0].id == first);
    CHECK(project.calques[0].name == "Calque 1");
    CHECK(project.calques[1].id == second);
    CHECK(project.calques[1].name == "Effets");
    // Les formes existantes restent dans le calque du fond.
    CHECK(project.calqueOfShape(a)->id == first);
    CHECK(project.calqueOfShape(b)->id == first);

    stack.undo(project);
    CHECK(project.calques.empty());
    CHECK(project.calqueOfShape(a) == nullptr); // retour au calque implicite
    stack.redo(project);
    CHECK(project.calques.size() == 2);
}

TEST_CASE("calques : supprimer replie les formes sur le calque du dessous, undo les rend") {
    ObjectId a;
    ObjectId b;
    auto project = project_with_two_shapes(a, b);
    commands::UndoStack stack;
    const ObjectId c1 = project.object_ids.next();
    const ObjectId c2 = project.object_ids.next();
    stack.execute(std::make_unique<commands::AddCalqueCommand>(c2, "Effets", c1), project);
    stack.execute(std::make_unique<commands::SetShapesCalqueCommand>(std::vector<ObjectId>{b}, c2),
                  project);
    CHECK(project.calqueOfShape(b)->id == c2);

    stack.execute(std::make_unique<commands::RemoveCalqueCommand>(c2), project);
    REQUIRE(project.calques.size() == 1);
    CHECK(project.calqueOfShape(b)->id == c1);

    stack.undo(project);
    REQUIRE(project.calques.size() == 2);
    CHECK(project.calqueOfShape(b)->id == c2);
    CHECK(project.calqueOfShape(a)->id == c1);
}

TEST_CASE("calques : le dernier calque ne se supprime pas") {
    ObjectId a;
    ObjectId b;
    auto project = project_with_two_shapes(a, b);
    commands::UndoStack stack;
    const ObjectId c1 = project.object_ids.next();
    const ObjectId c2 = project.object_ids.next();
    stack.execute(std::make_unique<commands::AddCalqueCommand>(c2, "Effets", c1), project);
    stack.execute(std::make_unique<commands::RemoveCalqueCommand>(c2), project);
    REQUIRE(project.calques.size() == 1);
    stack.execute(std::make_unique<commands::RemoveCalqueCommand>(c1), project); // sans effet
    CHECK(project.calques.size() == 1);
}

TEST_CASE("calques : renommer, masquer, verrouiller et deplacer sont annulables") {
    ObjectId a;
    ObjectId b;
    auto project = project_with_two_shapes(a, b);
    commands::UndoStack stack;
    const ObjectId c1 = project.object_ids.next();
    const ObjectId c2 = project.object_ids.next();
    stack.execute(std::make_unique<commands::AddCalqueCommand>(c2, "Effets", c1), project);

    stack.execute(std::make_unique<commands::RenameCalqueCommand>(c2, "Reflets"), project);
    CHECK(project.calques[1].name == "Reflets");
    stack.undo(project);
    CHECK(project.calques[1].name == "Effets");

    stack.execute(std::make_unique<commands::SetCalqueVisibleCommand>(c2, false), project);
    CHECK_FALSE(project.calques[1].visible);
    stack.undo(project);
    CHECK(project.calques[1].visible);

    stack.execute(std::make_unique<commands::SetCalqueLockedCommand>(c1, true), project);
    CHECK(project.shapeLockedByCalque(a));
    stack.undo(project);
    CHECK_FALSE(project.shapeLockedByCalque(a));

    stack.execute(std::make_unique<commands::MoveCalqueCommand>(c1, +1), project);
    CHECK(project.calques[0].id == c2);
    CHECK(project.calques[1].id == c1);
    stack.undo(project);
    CHECK(project.calques[0].id == c1);
    // Au bout de la pile : sans effet.
    stack.execute(std::make_unique<commands::MoveCalqueCommand>(c2, +1), project);
    CHECK(project.calques[1].id == c2);
}

TEST_CASE("calques : une forme creee entre dans le calque actif") {
    ObjectId a;
    ObjectId b;
    auto project = project_with_two_shapes(a, b);
    commands::UndoStack stack;
    const ObjectId c1 = project.object_ids.next();
    const ObjectId c2 = project.object_ids.next();
    stack.execute(std::make_unique<commands::AddCalqueCommand>(c2, "Effets", c1), project);
    project.active_calque = c2;
    document::VectorObject fresh;
    fresh.id = project.object_ids.next();
    stack.execute(std::make_unique<commands::AddVectorObjectCommand>(fresh), project);
    CHECK(project.calqueOfShape(fresh.id)->id == c2);
}

TEST_CASE("calques : renommer et verrouiller ne recalculent ni points ni segmentation") {
    // Portée exacte : un nom ou un verrou ne change aucune donnée calculée. Masquer, déplacer ou
    // réordonner change la séquence cousue, jamais les régions segmentées.
    const ObjectId id{7};
    CHECK_FALSE(commands::RenameCalqueCommand(id, "x").affectsEmbroidery());
    CHECK_FALSE(commands::RenameCalqueCommand(id, "x").affectsSegmentation());
    CHECK_FALSE(commands::SetCalqueLockedCommand(id, true).affectsEmbroidery());
    CHECK_FALSE(commands::SetCalqueLockedCommand(id, true).affectsSegmentation());
    CHECK_FALSE(commands::AddCalqueCommand(id, "x").affectsEmbroidery());
    CHECK_FALSE(commands::AddCalqueCommand(id, "x").affectsSegmentation());
    CHECK(commands::SetCalqueVisibleCommand(id, false).affectsEmbroidery());
    CHECK_FALSE(commands::SetCalqueVisibleCommand(id, false).affectsSegmentation());
    CHECK(commands::MoveCalqueCommand(id, 1).affectsEmbroidery());
    CHECK_FALSE(commands::MoveCalqueCommand(id, 1).affectsSegmentation());
    CHECK(commands::ReorderCalqueCommand(id, 0).affectsEmbroidery());
    CHECK_FALSE(commands::ReorderCalqueCommand(id, 0).affectsSegmentation());
    CHECK_FALSE(commands::SetShapesCalqueCommand({}, id).affectsSegmentation());
    // Supprimer peut emporter une source image : portée complète conservée.
    CHECK(commands::RemoveCalqueCommand(id).affectsSegmentation());
}

TEST_CASE("calques : reordonner place un calque a un rang en une etape, undo restaure") {
    document::Project project;
    project.calques = {
        {ObjectId{1}, "A"}, {ObjectId{2}, "B"}, {ObjectId{3}, "C"}, {ObjectId{4}, "D"}};
    commands::UndoStack stack;
    const auto names = [&] {
        std::string out;
        for (const auto& c : project.calques) {
            out += c.name;
        }
        return out;
    };
    stack.execute(std::make_unique<commands::ReorderCalqueCommand>(ObjectId{1}, 3), project);
    CHECK(names() == "BCDA");
    stack.execute(std::make_unique<commands::ReorderCalqueCommand>(ObjectId{4}, 0), project);
    CHECK(names() == "DBCA");
    stack.undo(project);
    CHECK(names() == "BCDA");
    stack.undo(project);
    CHECK(names() == "ABCD");
    stack.redo(project);
    CHECK(names() == "BCDA");
    // Rang hors bornes : ramené au dessus ; calque inconnu : sans effet.
    stack.execute(std::make_unique<commands::ReorderCalqueCommand>(ObjectId{2}, 99), project);
    CHECK(names() == "CDAB");
    stack.execute(std::make_unique<commands::ReorderCalqueCommand>(ObjectId{42}, 0), project);
    CHECK(names() == "CDAB");
    stack.undo(project);
    CHECK(names() == "CDAB");
}

TEST_CASE("calques : visibilite groupee en une etape, seuls les changements sont annules") {
    document::Project project;
    project.calques = {
        {ObjectId{1}, "A", true}, {ObjectId{2}, "B", false}, {ObjectId{3}, "C", true}};
    commands::UndoStack stack;
    stack.execute(std::make_unique<commands::SetCalquesVisibilityCommand>(
                      std::vector<std::pair<ObjectId, bool>>{
                          {ObjectId{1}, false}, {ObjectId{2}, true}, {ObjectId{3}, false}},
                      "Isoler le calque"),
                  project);
    CHECK_FALSE(project.calques[0].visible);
    CHECK(project.calques[1].visible);
    CHECK_FALSE(project.calques[2].visible);
    CHECK(stack.undoName() == "Isoler le calque");
    stack.undo(project);
    CHECK(project.calques[0].visible);
    CHECK_FALSE(project.calques[1].visible); // état d'origine, pas « tout visible »
    CHECK(project.calques[2].visible);
}
