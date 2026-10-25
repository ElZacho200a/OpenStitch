// SPDX-License-Identifier: Apache-2.0
// Insertion de nœud (HP-VEC-001), suppression d'un trou précis (HP-VEC-005) et contour autour
// d'objets (HP-VEC-006) : commandes annulables, objets d'origine intacts.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <utility>

#include "openstitch/commands/outline_ops.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/commands/vector_path_commands.hpp"
#include "openstitch/document/project.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

geometry::Path box(std::int32_t x0, std::int32_t x1, std::int32_t y0 = 0,
                   std::int32_t y1 = 10'000) {
    geometry::Path path;
    path.closed = true;
    for (const auto& [x, y] : {std::pair{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}) {
        path.nodes.push_back({Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner,
                              std::nullopt, std::nullopt});
    }
    return path;
}

ObjectId addBox(document::Project& project, std::int32_t x0, std::int32_t x1) {
    document::VectorObject object;
    object.id = project.object_ids.next();
    object.paths.push_back(geometry::PathSet{box(x0, x1), {}});
    project.vector_objects.push_back(std::move(object));
    return project.vector_objects.back().id;
}

} // namespace

TEST_CASE("insert node command keeps the shape and undo redo is exact", "[commands][insert]") {
    document::Project project;
    const ObjectId a = addBox(project, 0, 10'000);
    const auto before = project.findObject(a)->paths;
    UndoStack stack;

    auto result = make_insert_node_command(project, a, 0, 0, 0, 0.5);
    REQUIRE(result.command);
    stack.execute(std::move(result.command), project);
    const auto& nodes = project.findObject(a)->paths.front().outer.nodes;
    REQUIRE(nodes.size() == 5);
    CHECK(nodes[1].pos == Vec2um{Micrometers{5'000}, Micrometers{0}});

    REQUIRE(stack.undo(project));
    CHECK(project.findObject(a)->paths == before);
    REQUIRE(stack.redo(project));
    CHECK(project.findObject(a)->paths.front().outer.nodes.size() == 5);
}

TEST_CASE("insert node command refuses bad targets", "[commands][insert]") {
    document::Project project;
    const ObjectId a = addBox(project, 0, 10'000);
    CHECK_FALSE(make_insert_node_command(project, a, 0, 0, 9, 0.5).command);
    CHECK_FALSE(make_insert_node_command(project, a, 3, 0, 0, 0.5).command);
    CHECK_FALSE(make_insert_node_command(project, a, 0, 1, 0, 0.5).command);
    CHECK_FALSE(make_insert_node_command(project, a, 0, 0, 0, 0.0).command);
    CHECK_FALSE(make_insert_node_command(project, a, 0, 0, 0, 1.0).command);
    CHECK_FALSE(make_insert_node_command(project, ObjectId{999}, 0, 0, 0, 0.5).command);
}

TEST_CASE("remove hole at removes only the hole under the point", "[commands][holes]") {
    document::Project project;
    document::VectorObject object;
    object.id = project.object_ids.next();
    geometry::PathSet set;
    set.outer = box(0, 40'000, 0, 40'000);
    const geometry::Path holeA = box(5'000, 10'000, 5'000, 10'000);
    const geometry::Path holeB = box(25'000, 30'000, 25'000, 30'000);
    set.holes = {holeA, holeB};
    object.paths.push_back(set);
    const ObjectId id = object.id;
    project.vector_objects.push_back(std::move(object));
    UndoStack stack;

    CHECK_FALSE(
        make_remove_hole_at_command(project, id, Vec2um{Micrometers{20'000}, Micrometers{20'000}})
            .command);
    auto result =
        make_remove_hole_at_command(project, id, Vec2um{Micrometers{27'000}, Micrometers{27'000}});
    REQUIRE(result.command);
    stack.execute(std::move(result.command), project);
    const auto& holes = project.findObject(id)->paths.front().holes;
    REQUIRE(holes.size() == 1);
    CHECK(holes.front() == holeA);
    REQUIRE(stack.undo(project));
    CHECK(project.findObject(id)->paths.front().holes.size() == 2);
}

TEST_CASE("outline around two overlapping shapes adds one running object", "[commands][outline]") {
    document::Project project;
    const ObjectId a = addBox(project, 0, 10'000);
    const ObjectId b = addBox(project, 5'000, 20'000);
    const auto beforeA = project.findObject(a)->paths;
    UndoStack stack;

    OutlineOptions options;
    options.gap = Micrometers{1'000};
    auto result = make_outline_command(project, {a, b}, options);
    REQUIRE(result.command);
    stack.execute(std::move(result.command), project);

    REQUIRE(project.vector_objects.size() == 3);
    REQUIRE(project.embroidery_objects.size() == 1);
    const auto& outline = project.vector_objects.back();
    REQUIRE(outline.paths.size() == 1);
    // Union 0..20 mm agrandie de 1 mm : -1..21 mm.
    std::int32_t minX = 1 << 30;
    std::int32_t maxX = -(1 << 30);
    for (const auto& n : outline.paths.front().outer.nodes) {
        minX = std::min(minX, n.pos.x.value);
        maxX = std::max(maxX, n.pos.x.value);
    }
    CHECK(minX == -1'000);
    CHECK(maxX == 21'000);
    CHECK(std::holds_alternative<document::RunningStitchParams>(
        project.embroidery_objects.back().params));
    CHECK(project.findObject(a)->paths == beforeA);

    REQUIRE(stack.undo(project));
    CHECK(project.vector_objects.size() == 2);
    CHECK(project.embroidery_objects.empty());
}

TEST_CASE("outline satin makes a ring and triple uses three repeats", "[commands][outline]") {
    document::Project project;
    const ObjectId a = addBox(project, 0, 10'000);
    OutlineOptions options;
    options.kind = OutlineKind::Satin;
    options.satin_width = Micrometers{2'000};
    UndoStack stack;
    auto ring = make_outline_command(project, {a}, options);
    REQUIRE(ring.command);
    stack.execute(std::move(ring.command), project);
    CHECK(project.vector_objects.back().paths.front().holes.size() == 1);
    CHECK(std::holds_alternative<document::AutoSatinParams>(
        project.embroidery_objects.back().params));

    options.kind = OutlineKind::Triple;
    auto triple = make_outline_command(project, {a}, options);
    REQUIRE(triple.command);
    stack.execute(std::move(triple.command), project);
    const auto* rp =
        std::get_if<document::RunningStitchParams>(&project.embroidery_objects.back().params);
    REQUIRE(rp != nullptr);
    CHECK(rp->repeats == 3);
}

TEST_CASE("outline refuses an unknown selection", "[commands][outline]") {
    document::Project project;
    addBox(project, 0, 10'000);
    CHECK_FALSE(make_outline_command(project, {ObjectId{999}}, OutlineOptions{}).command);
}
