// SPDX-License-Identifier: Apache-2.0
// HP-FILE-007 : insertion d'un design dans le projet courant (une commande annulable).
#include <catch2/catch_test_macros.hpp>

#include "openstitch/commands/insert_design_command.hpp"
#include "openstitch/commands/transform_ops.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

ObjectId add_square(document::Project& project, std::int32_t x0) {
    geometry::PathSet set;
    set.outer.closed = true;
    for (const Vec2um p : {pt(x0, 0), pt(x0 + 5'000, 0), pt(x0 + 5'000, 5'000), pt(x0, 5'000)}) {
        set.outer.nodes.push_back(geometry::PathNode{p, geometry::NodeType::Corner, {}, {}});
    }
    document::VectorObject shape;
    shape.id = project.object_ids.next();
    shape.paths = {set};
    project.vector_objects.push_back(shape);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = shape.id;
    emb.params = document::TatamiParams{};
    project.embroidery_objects.push_back(emb);
    return shape.id;
}

document::ImportedDesign machine(std::int32_t x) {
    document::ImportedDesign design;
    design.source_format = "dst";
    design.sequence.commands = {
        {pt(x, 0), stitch::CommandType::Stitch, ObjectId{}},
        {pt(x + 100, 0), stitch::CommandType::Stitch, ObjectId{}},
        {pt(x + 100, 0), stitch::CommandType::End, ObjectId{}},
    };
    stitch::ColorBlock block;
    block.start = 0;
    block.end = 3;
    design.color_blocks = {block};
    return design;
}

} // namespace

TEST_CASE("InsertDesignCommand: fresh ids, translation, links rewritten, exact undo") {
    document::Project target;
    add_square(target, 0);
    add_square(target, 10'000);
    const auto before_last = target.object_ids.last();

    document::Project incoming;
    add_square(incoming, 0); // ids 1 et 2, en collision avec la cible
    incoming.active_calque = ObjectId{99};

    std::string why;
    auto command = make_insert_design_command(target, incoming, pt(30'000, 2'000), &why);
    REQUIRE(command);

    UndoStack stack;
    stack.execute(std::move(command), target);
    REQUIRE(target.vector_objects.size() == 3);
    REQUIRE(target.embroidery_objects.size() == 3);
    const auto& shape = target.vector_objects.back();
    const auto& emb = target.embroidery_objects.back();
    CHECK(shape.id.value > before_last);
    CHECK(emb.id.value > before_last);
    CHECK(emb.source_vector == shape.id);
    CHECK(shape.paths.front().outer.nodes.front().pos == pt(30'000, 2'000));
    // Les ids de la cible n'ont pas bougé.
    CHECK(target.vector_objects[0].id.value == 1);

    REQUIRE(stack.undo(target));
    CHECK(target.vector_objects.size() == 2);
    CHECK(target.embroidery_objects.size() == 2);
    REQUIRE(stack.redo(target));
    CHECK(target.vector_objects.size() == 3);
    CHECK(target.vector_objects.back().id == shape.id);
    // Jamais de réutilisation : un nouvel objet reçoit un id supérieur.
    CHECK(target.object_ids.next().value > emb.id.value);
}

TEST_CASE("InsertDesignCommand: machine sequence is appended and translated") {
    document::Project target;
    target.imported_design = machine(0);
    document::Project incoming;
    incoming.imported_design = machine(1'000);

    auto command = make_insert_design_command(target, incoming, pt(500, 700));
    REQUIRE(command);
    UndoStack stack;
    stack.execute(std::move(command), target);

    REQUIRE(target.imported_design.has_value());
    const auto& merged = *target.imported_design;
    // Fin de motif de la cible retirée : 2 + 3 commandes.
    REQUIRE(merged.sequence.commands.size() == 5);
    CHECK(merged.sequence.commands[2].pos == pt(1'500, 700));
    REQUIRE(merged.color_blocks.size() == 2);
    CHECK(merged.color_blocks[1].start == 2);
    CHECK(merged.color_blocks[1].end == 5);

    REQUIRE(stack.undo(target));
    CHECK(*target.imported_design == machine(0));
}

TEST_CASE("InsertDesignCommand: incompatible mixes are refused") {
    document::Project with_objects;
    add_square(with_objects, 0);
    document::Project with_machine;
    with_machine.imported_design = machine(0);

    std::string why;
    CHECK_FALSE(make_insert_design_command(with_objects, with_machine, pt(0, 0), &why));
    CHECK_FALSE(why.empty());
    CHECK_FALSE(make_insert_design_command(with_machine, with_objects, pt(0, 0), &why));
    CHECK_FALSE(make_insert_design_command(document::Project{}, document::Project{}, pt(0, 0)));
}
