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

namespace {

// Petite segmentation synthetique pour tester les commandes de regions.
document::Project project_with_segmentation() {
    document::Project project;
    project.original.width = 4;
    project.original.height = 1;
    project.original.rgba.assign(4 * 4, 255);

    segmentation::Segmentation seg;
    seg.width = 4;
    seg.height = 1;
    seg.labels = {1, 1, 2, 2};
    seg.region_slots.push_back(segmentation::Region{RegionId{1}, {255, 0, 0}, 2});
    seg.region_slots.push_back(segmentation::Region{RegionId{2}, {0, 0, 255}, 2});
    project.segmentation = std::move(seg);
    return project;
}

}  // namespace

TEST_CASE("AppendImageOpCommand invalide la segmentation et la restaure a l'annulation") {
    auto project = project_with_segmentation();
    UndoStack stack;

    stack.execute(std::make_unique<AppendImageOpCommand>(image::GrayscaleOp{}), project);
    CHECK_FALSE(project.segmentation.has_value());

    CHECK(stack.undo(project));
    REQUIRE(project.segmentation.has_value());
    CHECK(project.segmentation->region_count() == 2);
}

TEST_CASE("MergeRegionsCommand : undo restaure labels et comptes exactement") {
    auto project = project_with_segmentation();
    UndoStack stack;

    stack.execute(std::make_unique<MergeRegionsCommand>(RegionId{1}, RegionId{2}), project);
    CHECK(project.segmentation->region_count() == 1);
    CHECK(project.segmentation->find(RegionId{1})->pixel_count == 4);
    CHECK(project.segmentation->labels == std::vector<std::uint32_t>{1, 1, 1, 1});

    CHECK(stack.undo(project));
    CHECK(project.segmentation->region_count() == 2);
    CHECK(project.segmentation->find(RegionId{1})->pixel_count == 2);
    CHECK(project.segmentation->find(RegionId{2})->pixel_count == 2);
    CHECK(project.segmentation->labels == std::vector<std::uint32_t>{1, 1, 2, 2});

    CHECK(stack.redo(project));
    CHECK(project.segmentation->region_count() == 1);
}

TEST_CASE("RecolorRegionCommand : aller-retour exact") {
    auto project = project_with_segmentation();
    UndoStack stack;

    stack.execute(std::make_unique<RecolorRegionCommand>(RegionId{1},
                                                         std::array<std::uint8_t, 3>{9, 9, 9}),
                  project);
    CHECK(project.segmentation->find(RegionId{1})->rgb == std::array<std::uint8_t, 3>{9, 9, 9});
    CHECK(stack.undo(project));
    CHECK(project.segmentation->find(RegionId{1})->rgb == std::array<std::uint8_t, 3>{255, 0, 0});
}

TEST_CASE("SetSegmentationCommand : apply/revert echangent les etats") {
    document::Project project;
    UndoStack stack;
    CHECK_FALSE(project.segmentation.has_value());

    auto seg = project_with_segmentation().segmentation;
    stack.execute(std::make_unique<SetSegmentationCommand>(std::move(seg)), project);
    CHECK(project.segmentation.has_value());

    CHECK(stack.undo(project));
    CHECK_FALSE(project.segmentation.has_value());
    CHECK(stack.redo(project));
    CHECK(project.segmentation.has_value());
}

TEST_CASE("AddVectorObject et MoveNode : undo/redo coherents") {
    document::Project project;
    UndoStack stack;

    document::VectorObject object;
    object.id = project.object_ids.next();
    object.name = "test";
    geometry::Path square;
    square.closed = true;
    square.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{0}},
                                              geometry::NodeType::Corner, std::nullopt,
                                              std::nullopt});
    square.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{1000}, Micrometers{0}},
                                              geometry::NodeType::Corner, std::nullopt,
                                              std::nullopt});
    square.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{1000}},
                                              geometry::NodeType::Corner, std::nullopt,
                                              std::nullopt});
    object.paths.push_back(geometry::PathSet{square, {}});

    stack.execute(std::make_unique<AddVectorObjectCommand>(object), project);
    REQUIRE(project.vector_objects.size() == 1);
    const ObjectId id = project.vector_objects[0].id;

    const Vec2um oldPos{Micrometers{0}, Micrometers{0}};
    const Vec2um newPos{Micrometers{500}, Micrometers{500}};
    stack.execute(std::make_unique<MoveNodeCommand>(id, document::NodeRef{0, 0, 0}, oldPos, newPos),
                  project);
    CHECK(project.findObject(id)->paths[0].outer.nodes[0].pos == newPos);

    CHECK(stack.undo(project));
    CHECK(project.findObject(id)->paths[0].outer.nodes[0].pos == oldPos);
    CHECK(stack.undo(project));
    CHECK(project.vector_objects.empty());
    CHECK(stack.redo(project));
    CHECK(stack.redo(project));
    CHECK(project.findObject(id)->paths[0].outer.nodes[0].pos == newPos);
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
