// SPDX-License-Identifier: Apache-2.0
//
// HP-OBJ-024 : commandes sur les calques du document (ajout, suppression, renommage,
// visibilité, verrou, déplacement, affectation de formes), toutes annulables.
#include <catch2/catch_test_macros.hpp>

#include <memory>
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
