// SPDX-License-Identifier: Apache-2.0
//
// HP-OBJ-009 (groupes), HP-OBJ-011 (ordre d'empilement) et HP-OBJ-015 (position / taille
// numériques) : commandes annulables et fonctions pures du cœur.
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "openstitch/commands/group_commands.hpp"
#include "openstitch/commands/order_ops.hpp"
#include "openstitch/commands/placement_ops.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"

using namespace openstitch;

namespace {

// Carré de côté `sideUm` dont le coin bas-gauche est en (x, y).
document::VectorObject square(document::Project& project, int x, int y, int sideUm) {
    document::VectorObject v;
    v.id = project.object_ids.next();
    geometry::PathSet set;
    set.outer.closed = true;
    const int xs[] = {x, x + sideUm, x + sideUm, x};
    const int ys[] = {y, y, y + sideUm, y + sideUm};
    for (int i = 0; i < 4; ++i) {
        geometry::PathNode node;
        node.pos = Vec2um{Micrometers{xs[i]}, Micrometers{ys[i]}};
        set.outer.nodes.push_back(node);
    }
    v.paths.push_back(set);
    return v;
}

struct Fixture {
    document::Project project;
    ObjectId a, b, c;
    Fixture() {
        auto va = square(project, 0, 0, 10'000);
        auto vb = square(project, 20'000, 0, 10'000);
        auto vc = square(project, 40'000, 0, 10'000);
        a = va.id;
        b = vb.id;
        c = vc.id;
        project.vector_objects = {va, vb, vc};
    }
};

} // namespace

TEST_CASE("groups : grouper puis annuler restaure l'etat exact") {
    Fixture f;
    commands::UndoStack stack;
    const ObjectId g = f.project.object_ids.next();
    stack.execute(std::make_unique<commands::GroupShapesCommand>(g, "Duo", std::vector{f.a, f.b}),
                  f.project);
    REQUIRE(f.project.groups.size() == 1);
    CHECK(f.project.findObject(f.a)->group == g);
    CHECK(f.project.findObject(f.b)->group == g);
    CHECK_FALSE(f.project.findObject(f.c)->group.valid());
    CHECK(f.project.shapesOfGroup(g) == std::vector{f.a, f.b});
    stack.undo(f.project);
    CHECK(f.project.groups.empty());
    CHECK_FALSE(f.project.findObject(f.a)->group.valid());
    stack.redo(f.project);
    CHECK(f.project.groups.size() == 1);
}

TEST_CASE("groups : un seul element ne forme pas de groupe") {
    Fixture f;
    commands::UndoStack stack;
    const ObjectId g = f.project.object_ids.next();
    stack.execute(std::make_unique<commands::GroupShapesCommand>(g, "Seul", std::vector{f.a}),
                  f.project);
    CHECK(f.project.groups.empty());
    CHECK_FALSE(f.project.findObject(f.a)->group.valid());
}

TEST_CASE("groups : imbrication, expansion de la selection et degroupage") {
    Fixture f;
    commands::UndoStack stack;
    const ObjectId inner = f.project.object_ids.next();
    stack.execute(
        std::make_unique<commands::GroupShapesCommand>(inner, "Interieur", std::vector{f.a, f.b}),
        f.project);
    const ObjectId outer = f.project.object_ids.next();
    stack.execute(
        std::make_unique<commands::GroupShapesCommand>(outer, "Exterieur", std::vector{f.a, f.c}),
        f.project);
    REQUIRE(f.project.groups.size() == 2);
    // `a` etait groupee : c'est son groupe de premier niveau qui devient enfant.
    CHECK(f.project.findGroup(inner)->parent == outer);
    CHECK(f.project.findObject(f.c)->group == outer);
    CHECK(f.project.topGroupOfShape(f.b) == outer);
    CHECK(f.project.expandToGroups({f.b}) == std::vector{f.b, f.a, f.c});

    // Degrouper l'exterieur rend l'interieur au premier niveau.
    stack.execute(std::make_unique<commands::UngroupCommand>(outer), f.project);
    REQUIRE(f.project.groups.size() == 1);
    CHECK_FALSE(f.project.findGroup(inner)->parent.valid());
    CHECK_FALSE(f.project.findObject(f.c)->group.valid());
    CHECK(f.project.expandToGroups({f.a}) == std::vector{f.a, f.b});
    stack.undo(f.project);
    REQUIRE(f.project.groups.size() == 2);
    CHECK(f.project.findGroup(inner)->parent == outer);
    CHECK(f.project.findObject(f.c)->group == outer);
}

TEST_CASE("groups : un groupe inconnu ou cyclique ne bloque pas les requetes") {
    Fixture f;
    f.project.findObject(f.a)->group = ObjectId{999};
    CHECK_FALSE(f.project.topGroupOfShape(f.a).valid());
    CHECK(f.project.expandToGroups({f.a}) == std::vector{f.a});
    f.project.groups = {{ObjectId{7}, "x", ObjectId{8}}, {ObjectId{8}, "y", ObjectId{7}}};
    f.project.findObject(f.b)->group = ObjectId{7};
    CHECK(f.project.topGroupOfShape(f.b).valid()); // termine malgre le cycle
}

TEST_CASE("order : premier plan, arriere-plan, avancer et reculer") {
    const std::vector<ObjectId> order{ObjectId{1}, ObjectId{2}, ObjectId{3}, ObjectId{4}};
    using commands::StackMove;
    using V = std::vector<ObjectId>;
    CHECK(commands::stacked_order(order, {ObjectId{2}}, StackMove::ToFront) ==
          V{ObjectId{1}, ObjectId{3}, ObjectId{4}, ObjectId{2}});
    CHECK(commands::stacked_order(order, {ObjectId{3}, ObjectId{1}}, StackMove::ToBack) ==
          V{ObjectId{1}, ObjectId{3}, ObjectId{2}, ObjectId{4}});
    CHECK(commands::stacked_order(order, {ObjectId{1}, ObjectId{2}}, StackMove::Forward) ==
          V{ObjectId{3}, ObjectId{1}, ObjectId{2}, ObjectId{4}});
    CHECK(commands::stacked_order(order, {ObjectId{3}}, StackMove::Backward) ==
          V{ObjectId{1}, ObjectId{3}, ObjectId{2}, ObjectId{4}});
    // Deja au bord : rien ne bouge.
    CHECK(commands::stacked_order(order, {ObjectId{4}}, StackMove::Forward) == order);
    CHECK(commands::stacked_order(order, {ObjectId{1}}, StackMove::Backward) == order);
    CHECK(commands::stacked_order(order, {ObjectId{4}}, StackMove::ToFront) == order);
}

TEST_CASE("placement : centre et taille numeriques, une commande, annulation exacte") {
    Fixture f;
    commands::UndoStack stack;
    const std::vector<ObjectId> ids{f.a, f.b};
    const auto before = commands::selection_geometry(f.project, ids);
    REQUIRE(before);
    CHECK(before->width.value == 30'000);
    CHECK(before->height.value == 10'000);
    CHECK(before->center.x.value == 15'000);

    auto cmd = commands::make_place_command(f.project, ids, Micrometers{0}, Micrometers{5'000},
                                            Micrometers{60'000}, std::nullopt, "test");
    REQUIRE(cmd);
    stack.execute(std::move(cmd), f.project);
    const auto after = commands::selection_geometry(f.project, ids);
    REQUIRE(after);
    CHECK(after->width.value == 60'000);
    CHECK(after->height.value == 10'000);
    CHECK(after->center.x.value == 0);
    CHECK(after->center.y.value == 5'000);
    stack.undo(f.project);
    const auto restored = commands::selection_geometry(f.project, ids);
    CHECK(restored->width.value == 30'000);
    CHECK(restored->center.x.value == 15'000);

    // Rien ne change : pas de commande.
    CHECK(commands::make_place_command(f.project, ids, std::nullopt, std::nullopt, std::nullopt,
                                       std::nullopt, "x") == nullptr);
}

TEST_CASE("placement : rotation de 90 degres echange largeur et hauteur") {
    Fixture f;
    commands::UndoStack stack;
    const std::vector<ObjectId> ids{f.a, f.b};
    auto cmd = commands::make_rotate_about_center_command(f.project, ids, 90.0, "rot");
    REQUIRE(cmd);
    stack.execute(std::move(cmd), f.project);
    const auto after = commands::selection_geometry(f.project, ids);
    CHECK(after->width.value == 10'000);
    CHECK(after->height.value == 30'000);
    CHECK(commands::make_rotate_about_center_command(f.project, ids, 0.0, "x") == nullptr);
}
