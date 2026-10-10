// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
#include <vector>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/commands/vector_ops.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/boolean.hpp"

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

ObjectId add_object(document::Project& project, geometry::PathSet shape, bool withTatami) {
    document::VectorObject object;
    object.id = project.object_ids.next();
    object.name = "Forme";
    object.paths = {std::move(shape)};
    project.vector_objects.push_back(object);
    if (withTatami) {
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "Tatami";
        emb.source_vector = object.id;
        emb.params = document::TatamiParams{};
        project.embroidery_objects.push_back(emb);
    }
    return object.id;
}

double area_mm2(const document::VectorObject& object) {
    double total = 0.0;
    for (const auto& set : object.paths) {
        total += geometry::path_set_area_um2(set);
    }
    return total / 1.0e6;
}

} // namespace

TEST_CASE("vector ops: union merges shapes into the active one and drops the others",
          "[vector_ops]") {
    document::Project project;
    const ObjectId a = add_object(project, rect(0, 0, 10'000, 10'000), true);
    const ObjectId b = add_object(project, rect(5'000, 0, 15'000, 10'000), true);
    UndoStack stack;

    auto result = make_boolean_command(project, BooleanOp::Union, {a, b});
    REQUIRE(result.command);
    stack.execute(std::move(result.command), project);

    REQUIRE(project.vector_objects.size() == 1);
    CHECK(project.vector_objects[0].id == b);
    CHECK(area_mm2(project.vector_objects[0]) == 150.0);
    REQUIRE(project.embroidery_objects.size() == 1);
    CHECK(project.embroidery_objects[0].source_vector == b);

    REQUIRE(stack.undo(project));
    CHECK(project.vector_objects.size() == 2);
    CHECK(project.embroidery_objects.size() == 2);
    CHECK(area_mm2(*project.findObject(b)) == 100.0);
}

TEST_CASE("vector ops: subtract notches the lowest shape and removes the cutter", "[vector_ops]") {
    document::Project project;
    const ObjectId base = add_object(project, rect(0, 0, 10'000, 10'000), true);
    const ObjectId cutter = add_object(project, rect(5'000, 0, 20'000, 10'000), false);
    UndoStack stack;

    // L'ordre de sélection ne compte pas : c'est la forme la plus basse du document qui reste.
    auto result = make_boolean_command(project, BooleanOp::Subtract, {base, cutter});
    REQUIRE(result.command);
    stack.execute(std::move(result.command), project);
    REQUIRE(project.vector_objects.size() == 1);
    CHECK(project.vector_objects[0].id == base);
    CHECK(area_mm2(project.vector_objects[0]) == 50.0);
    CHECK(project.embroidery_objects.size() == 1);
}

TEST_CASE("vector ops: intersect keeps the common part and refuses disjoint shapes",
          "[vector_ops]") {
    document::Project project;
    const ObjectId a = add_object(project, rect(0, 0, 10'000, 10'000), false);
    const ObjectId b = add_object(project, rect(5'000, 5'000, 15'000, 15'000), false);
    const ObjectId far = add_object(project, rect(50'000, 50'000, 60'000, 60'000), false);

    auto ok = make_boolean_command(project, BooleanOp::Intersect, {a, b});
    REQUIRE(ok.command);
    UndoStack stack;
    stack.execute(std::move(ok.command), project);
    CHECK(area_mm2(*project.findObject(b)) == 25.0);

    auto empty = make_boolean_command(project, BooleanOp::Intersect, {b, far});
    CHECK_FALSE(empty.command);
    CHECK_FALSE(empty.error.empty());
}

TEST_CASE("vector ops: boolean needs two shapes", "[vector_ops]") {
    document::Project project;
    const ObjectId a = add_object(project, rect(0, 0, 10'000, 10'000), false);
    auto result = make_boolean_command(project, BooleanOp::Union, {a});
    CHECK_FALSE(result.command);
}

TEST_CASE("vector ops: split cuts a shape in two and clones its fill", "[vector_ops]") {
    document::Project project;
    const ObjectId id = add_object(project, rect(0, 0, 20'000, 10'000), true);
    UndoStack stack;

    auto result = make_split_command(project, {id}, pt(10'000, -5'000), pt(10'000, 15'000));
    REQUIRE(result.command);
    stack.execute(std::move(result.command), project);

    REQUIRE(project.vector_objects.size() == 2);
    REQUIRE(project.embroidery_objects.size() == 2);
    CHECK(project.vector_objects[0].id == id);
    CHECK(project.embroidery_objects[1].source_vector == project.vector_objects[1].id);
    const double total = area_mm2(project.vector_objects[0]) + area_mm2(project.vector_objects[1]);
    CHECK(total > 199.0);
    CHECK(total <= 200.0);

    REQUIRE(stack.undo(project));
    CHECK(project.vector_objects.size() == 1);
    CHECK(project.embroidery_objects.size() == 1);
    CHECK(area_mm2(project.vector_objects[0]) == 200.0);
}

TEST_CASE("vector ops: split refuses a line that misses the shape", "[vector_ops]") {
    document::Project project;
    const ObjectId id = add_object(project, rect(0, 0, 10'000, 10'000), false);
    auto result = make_split_command(project, {id}, pt(50'000, -5'000), pt(50'000, 15'000));
    CHECK_FALSE(result.command);
    auto degenerate = make_split_command(project, {id}, pt(1, 1), pt(1, 1));
    CHECK_FALSE(degenerate.command);
}

TEST_CASE("vector ops: break apart separates disjoint pieces", "[vector_ops]") {
    document::Project project;
    const ObjectId id = add_object(project, rect(0, 0, 10'000, 10'000), true);
    project.findObject(id)->paths.push_back(rect(30'000, 0, 40'000, 5'000));
    UndoStack stack;

    auto result = make_break_apart_command(project, id);
    REQUIRE(result.command);
    stack.execute(std::move(result.command), project);
    REQUIRE(project.vector_objects.size() == 2);
    CHECK(project.vector_objects[0].paths.size() == 1);
    CHECK(project.embroidery_objects.size() == 2);

    REQUIRE(stack.undo(project));
    REQUIRE(project.vector_objects.size() == 1);
    CHECK(project.vector_objects[0].paths.size() == 2);

    auto single = make_break_apart_command(project, id);
    stack.execute(std::move(single.command), project);
    auto again = make_break_apart_command(project, project.vector_objects[0].id);
    CHECK_FALSE(again.command);
}
