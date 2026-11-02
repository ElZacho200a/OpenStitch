// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
#include <vector>

#include "openstitch/commands/cleanup_ops.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

geometry::PathSet rect(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    geometry::PathSet set;
    for (const Vec2um p : {pt(x0, y0), pt(x1, y0), pt(x1, y1), pt(x0, y1)}) {
        set.outer.nodes.push_back(geometry::PathNode{p, geometry::NodeType::Corner, {}, {}});
    }
    return set;
}

// Rectangle bruité : de nombreux nœuds quasi alignés sur chaque arête.
geometry::PathSet noisy_rect() {
    geometry::PathSet set;
    for (int i = 0; i <= 20; ++i) {
        set.outer.nodes.push_back(
            geometry::PathNode{pt(i * 1000, (i % 2) * 5), geometry::NodeType::Corner, {}, {}});
    }
    for (const Vec2um p : {pt(20000, 10000), pt(0, 10000)}) {
        set.outer.nodes.push_back(geometry::PathNode{p, geometry::NodeType::Corner, {}, {}});
    }
    return set;
}

ObjectId add_object(document::Project& project, std::vector<geometry::PathSet> shape) {
    document::VectorObject object;
    object.id = project.object_ids.next();
    object.name = "Forme";
    object.paths = std::move(shape);
    project.vector_objects.push_back(object);
    return object.id;
}

} // namespace

TEST_CASE("cleanup ops: remove small pieces keeps big ones and undoes in one step",
          "[cleanup_ops]") {
    document::Project project;
    const ObjectId id = add_object(
        project, {rect(0, 0, 10000, 10000), rect(20000, 0, 20100, 100), rect(30000, 0, 30100, 100)});
    UndoStack stack;
    auto made = make_remove_small_pieces_command(project, {id}, 1.0e6);
    REQUIRE(made.command);
    CHECK(made.resulting_objects == 2);
    stack.execute(std::move(made.command), project);
    CHECK(project.findObject(id)->paths.size() == 1);
    stack.undo(project);
    CHECK(project.findObject(id)->paths.size() == 3);
}

TEST_CASE("cleanup ops: remove small pieces never empties a shape and refuses a no-op",
          "[cleanup_ops]") {
    document::Project project;
    const ObjectId tiny = add_object(project, {rect(0, 0, 100, 100), rect(500, 0, 600, 100)});
    CHECK_FALSE(make_remove_small_pieces_command(project, {tiny}, 1.0e9).command);
    const ObjectId big = add_object(project, {rect(0, 0, 10000, 10000), rect(20000, 0, 30000, 10000)});
    auto made = make_remove_small_pieces_command(project, {big}, 1.0);
    CHECK_FALSE(made.command);
    CHECK_FALSE(made.error.empty());
}

TEST_CASE("cleanup ops: simplify drops near-collinear nodes and undoes", "[cleanup_ops]") {
    document::Project project;
    const ObjectId id = add_object(project, {noisy_rect()});
    const std::size_t before = project.findObject(id)->paths.front().outer.nodes.size();
    UndoStack stack;
    auto made = make_reshape_paths_command(project, {id}, ReshapeKind::Simplify, Micrometers{50});
    REQUIRE(made.command);
    stack.execute(std::move(made.command), project);
    CHECK(project.findObject(id)->paths.front().outer.nodes.size() < before);
    stack.undo(project);
    CHECK(project.findObject(id)->paths.front().outer.nodes.size() == before);
}

TEST_CASE("cleanup ops: smooth refuses an invalid tolerance and skips locked shapes",
          "[cleanup_ops]") {
    document::Project project;
    const ObjectId id = add_object(project, {noisy_rect()});
    CHECK_FALSE(make_reshape_paths_command(project, {id}, ReshapeKind::Smooth, Micrometers{0})
                    .command);
    project.findObject(id)->locked = true;
    CHECK_FALSE(make_reshape_paths_command(project, {id}, ReshapeKind::Simplify, Micrometers{50})
                    .command);
}
