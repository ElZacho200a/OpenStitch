// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "openstitch/commands/import_machine_design_command.hpp"
#include "openstitch/commands/stitch_point_edit_commands.hpp"
#include "openstitch/commands/undo_stack.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

document::Project project_with_object() {
    document::Project project;
    document::EmbroideryObject e;
    e.id = project.object_ids.next();
    project.embroidery_objects.push_back(e);
    return project;
}

document::Project project_with_import() {
    document::Project project;
    document::ImportedDesign d;
    d.source_format = "dst";
    for (int i = 0; i < 6; ++i) {
        d.sequence.commands.push_back({pt(i * 1'000, 0), stitch::CommandType::Stitch, ObjectId{}});
    }
    d.sequence.commands.push_back({pt(0, 0), stitch::CommandType::ColorChange, ObjectId{}});
    d.sequence.commands.push_back({pt(0, 500), stitch::CommandType::Stitch, ObjectId{}});
    stitch::ColorBlock b1;
    b1.start = 0;
    b1.end = 7;
    stitch::ColorBlock b2;
    b2.start = 7;
    b2.end = 8;
    d.color_blocks = {b1, b2};
    project.imported_design = d;
    return project;
}

} // namespace

TEST_CASE("EditStitchPointsCommand : deplacement groupe en un seul undo") {
    auto project = project_with_object();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;
    stack.execute(std::make_unique<EditStitchPointsCommand>(EditStitchPointsCommand::move_points(
                      id, {{1, pt(10, 10)}, {3, pt(30, 30)}}, 77, 5)),
                  project);
    const auto* obj = project.findEmbroidery(id);
    CHECK(obj->overrides.size() == 2);
    CHECK(obj->edited_fingerprint == 77);
    CHECK(obj->edited_point_count == 5);

    CHECK(stack.undo(project));
    CHECK(obj->overrides.empty());
    CHECK(obj->edited_fingerprint == 0);
    CHECK(stack.redo(project));
    CHECK(obj->overrides.size() == 2);
}

TEST_CASE("EditStitchPointsCommand : suppression groupee puis restauration") {
    auto project = project_with_object();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;
    stack.execute(std::make_unique<EditStitchPointsCommand>(
                      EditStitchPointsCommand::delete_points(id, {1, 2, 4}, 9, 6)),
                  project);
    const auto* obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 3);
    CHECK(obj->overrides[0].deleted);

    stack.execute(std::make_unique<EditStitchPointsCommand>(
                      EditStitchPointsCommand::restore_points(id, {1, 2, 4}, 9, 6)),
                  project);
    CHECK(obj->overrides.empty()); // plus aucune entree vide
    CHECK(obj->edited_point_count == 0);
    CHECK(stack.undo(project));
    CHECK(obj->overrides.size() == 3);
}

TEST_CASE("EditStitchPointsCommand : insertion apres un point") {
    auto project = project_with_object();
    const ObjectId id = project.embroidery_objects[0].id;
    UndoStack stack;
    stack.execute(std::make_unique<EditStitchPointsCommand>(
                      EditStitchPointsCommand::insert_point(id, 2, pt(5, 5), 9, 6)),
                  project);
    stack.execute(std::make_unique<EditStitchPointsCommand>(
                      EditStitchPointsCommand::insert_point(id, 2, pt(6, 6), 9, 6)),
                  project);
    const auto* obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 1);
    REQUIRE(obj->overrides[0].inserted_after.size() == 2);
    CHECK(obj->overrides[0].inserted_after[1] == pt(6, 6));
    CHECK(stack.undo(project));
    CHECK(obj->overrides[0].inserted_after.size() == 1);
}

TEST_CASE("EditStitchPointsCommand : index hors vue ou objet Dirty -- aucune mutation") {
    auto project = project_with_object();
    const ObjectId id = project.embroidery_objects[0].id;
    auto bad = EditStitchPointsCommand::delete_points(id, {1, 99}, 9, 6);
    bad.apply(project);
    CHECK_FALSE(bad.applied());
    CHECK(project.findEmbroidery(id)->overrides.empty());

    auto first = EditStitchPointsCommand::delete_points(id, {1}, 9, 6);
    first.apply(project);
    REQUIRE(first.applied());
    auto dirty = EditStitchPointsCommand::delete_points(id, {2}, 10, 6); // empreinte differente
    dirty.apply(project);
    CHECK_FALSE(dirty.applied());
    CHECK(project.findEmbroidery(id)->overrides.size() == 1);
}

TEST_CASE("EditStitchPointsCommand : type et coupe groupes") {
    auto project = project_with_object();
    const ObjectId id = project.embroidery_objects[0].id;
    auto t = EditStitchPointsCommand::set_type(id, {1, 2}, document::StitchPointType::Jump, 9, 6);
    t.apply(project);
    auto c = EditStitchPointsCommand::set_trim(id, {2}, true, 9, 6);
    c.apply(project);
    const auto* obj = project.findEmbroidery(id);
    REQUIRE(obj->overrides.size() == 2);
    CHECK(obj->overrides[1].trim_after);
    c.revert(project);
    CHECK_FALSE(obj->overrides[1].trim_after);
}

TEST_CASE("EditImportedPointsCommand : deplacement et type d'un design importe") {
    auto project = project_with_import();
    UndoStack stack;
    stack.execute(std::make_unique<EditImportedPointsCommand>(
                      EditImportedPointsCommand::move_points({{1, pt(7, 7)}, {2, pt(8, 8)}})),
                  project);
    CHECK(project.imported_design->sequence.commands[1].pos == pt(7, 7));
    CHECK(project.imported_design->sequence.commands[1].pass == stitch::StitchPass::Manual);
    stack.execute(std::make_unique<EditImportedPointsCommand>(
                      EditImportedPointsCommand::set_type({3}, document::StitchPointType::Jump)),
                  project);
    CHECK(project.imported_design->sequence.commands[3].type == stitch::CommandType::Jump);
    CHECK(stack.undo(project));
    CHECK(project.imported_design->sequence.commands[3].type == stitch::CommandType::Stitch);
    CHECK(stack.undo(project));
    CHECK(project.imported_design->sequence.commands[1].pos == pt(1'000, 0));
}

TEST_CASE("EditImportedPointsCommand : suppression recale les blocs de couleur") {
    auto project = project_with_import();
    UndoStack stack;
    stack.execute(std::make_unique<EditImportedPointsCommand>(
                      EditImportedPointsCommand::delete_points({4, 1})),
                  project);
    const auto& d = *project.imported_design;
    CHECK(d.sequence.commands.size() == 6);
    CHECK(d.color_blocks[0].start == 0);
    CHECK(d.color_blocks[0].end == 5);
    CHECK(d.color_blocks[1].start == 5);
    CHECK(d.color_blocks[1].end == 6);
    CHECK(stack.undo(project));
    CHECK(project.imported_design->sequence.commands.size() == 8);
    CHECK(project.imported_design->color_blocks[1].start == 7);
}

TEST_CASE("EditImportedPointsCommand : insertion et refus sur une commande non editable") {
    auto project = project_with_import();
    UndoStack stack;
    stack.execute(std::make_unique<EditImportedPointsCommand>(
                      EditImportedPointsCommand::insert_point(2, pt(2'500, 100))),
                  project);
    REQUIRE(project.imported_design->sequence.commands.size() == 9);
    CHECK(project.imported_design->sequence.commands[3].pos == pt(2'500, 100));
    CHECK(project.imported_design->color_blocks[0].end == 8);
    CHECK(project.imported_design->color_blocks[1].start == 8);

    auto refused = EditImportedPointsCommand::move_points({{7, pt(1, 1)}}); // ColorChange decale
    refused.apply(project);
    CHECK_FALSE(refused.applied());
    CHECK(project.imported_design->sequence.commands.size() == 9);
}
