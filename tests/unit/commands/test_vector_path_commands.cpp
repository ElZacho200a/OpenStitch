// SPDX-License-Identifier: Apache-2.0
// Lisser (HP-VEC-007) et Convertir en courbes (HP-VEC-008) : commandes annulables.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/commands/vector_path_commands.hpp"
#include "openstitch/document/project.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

geometry::Path polygonCircle(std::int32_t cx, std::int32_t radius, int sides) {
    geometry::Path path;
    path.closed = true;
    for (int i = 0; i < sides; ++i) {
        const double a = 2.0 * std::numbers::pi * i / sides;
        path.nodes.push_back(
            {Vec2um{Micrometers{cx + static_cast<std::int32_t>(std::lround(radius * std::cos(a)))},
                    Micrometers{static_cast<std::int32_t>(std::lround(radius * std::sin(a)))}},
             geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    return path;
}

geometry::Path square(std::int32_t side) {
    geometry::Path path;
    path.closed = true;
    for (const auto& [x, y] : {std::pair{0, 0}, {side, 0}, {side, side}, {0, side}}) {
        path.nodes.push_back({Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner,
                              std::nullopt, std::nullopt});
    }
    return path;
}

ObjectId addObject(document::Project& project, geometry::Path outer) {
    document::VectorObject object;
    object.id = project.object_ids.next();
    object.paths.push_back(geometry::PathSet{std::move(outer), {}});
    project.vector_objects.push_back(std::move(object));
    return project.vector_objects.back().id;
}

} // namespace

TEST_CASE("smooth command reduces nodes and undo redo is exact", "[commands][smooth]") {
    document::Project project;
    const ObjectId a = addObject(project, polygonCircle(0, 10'000, 72));
    const ObjectId b = addObject(project, polygonCircle(40'000, 8'000, 60));
    const auto beforeA = project.findObject(a)->paths;
    const auto beforeB = project.findObject(b)->paths;

    auto result = make_smooth_command(project, {a, b}, Micrometers{200});
    REQUIRE(result.command != nullptr);
    CHECK(result.resulting_objects == 2);
    CHECK(result.command->name() == "Lisser");
    // La fabrique ne touche pas au document.
    CHECK(project.findObject(a)->paths == beforeA);

    UndoStack stack;
    stack.execute(std::move(result.command), project);
    const auto afterA = project.findObject(a)->paths;
    const auto afterB = project.findObject(b)->paths;
    CHECK(afterA != beforeA);
    CHECK(afterA.front().outer.nodes.size() < beforeA.front().outer.nodes.size());
    CHECK(afterB.front().outer.nodes.size() < beforeB.front().outer.nodes.size());

    // Une seule étape d'annulation pour toute la sélection.
    REQUIRE(stack.undo(project));
    CHECK(project.findObject(a)->paths == beforeA);
    CHECK(project.findObject(b)->paths == beforeB);
    CHECK_FALSE(stack.canUndo());
    REQUIRE(stack.redo(project));
    CHECK(project.findObject(a)->paths == afterA);
    CHECK(project.findObject(b)->paths == afterB);
}

TEST_CASE("smooth command is refused when nothing changes", "[commands][smooth]") {
    document::Project project;
    const ObjectId sq = addObject(project, square(10'000));
    auto result = make_smooth_command(project, {sq}, Micrometers{200});
    CHECK(result.command == nullptr);
    CHECK_FALSE(result.error.empty());
    auto none = make_smooth_command(project, {ObjectId{999}}, Micrometers{200});
    CHECK(none.command == nullptr);
    auto zero = make_smooth_command(project, {sq}, Micrometers{0});
    CHECK(zero.command == nullptr);
}

TEST_CASE("smooth command is deterministic", "[commands][smooth]") {
    document::Project p1;
    document::Project p2;
    const ObjectId a = addObject(p1, polygonCircle(0, 10'000, 72));
    const ObjectId b = addObject(p2, polygonCircle(0, 10'000, 72));
    auto r1 = make_smooth_command(p1, {a}, Micrometers{250});
    auto r2 = make_smooth_command(p2, {b}, Micrometers{250});
    REQUIRE(r1.command != nullptr);
    REQUIRE(r2.command != nullptr);
    r1.command->apply(p1);
    r2.command->apply(p2);
    CHECK(p1.findObject(a)->paths == p2.findObject(b)->paths);
}

TEST_CASE("convert to curves command adds tangents and undo redo is exact", "[commands][curves]") {
    document::Project project;
    const ObjectId a = addObject(project, polygonCircle(0, 10'000, 24));
    const ObjectId sq = addObject(project, square(10'000)); // coins francs : inchangé
    const auto beforeA = project.findObject(a)->paths;
    const auto beforeSq = project.findObject(sq)->paths;

    auto result = make_convert_to_curves_command(project, {a, sq});
    REQUIRE(result.command != nullptr);
    CHECK(result.resulting_objects == 1);
    CHECK(result.command->name() == "Convertir en courbes");

    UndoStack stack;
    stack.execute(std::move(result.command), project);
    const auto afterA = project.findObject(a)->paths;
    bool anyTangent = false;
    for (const auto& node : afterA.front().outer.nodes) {
        anyTangent = anyTangent || node.tan_in.has_value() || node.tan_out.has_value();
    }
    CHECK(anyTangent);
    CHECK(project.findObject(sq)->paths == beforeSq);

    REQUIRE(stack.undo(project));
    CHECK(project.findObject(a)->paths == beforeA);
    REQUIRE(stack.redo(project));
    CHECK(project.findObject(a)->paths == afterA);
}

TEST_CASE("convert to curves command is refused on sharp polygons", "[commands][curves]") {
    document::Project project;
    const ObjectId sq = addObject(project, square(10'000));
    auto result = make_convert_to_curves_command(project, {sq});
    CHECK(result.command == nullptr);
    CHECK_FALSE(result.error.empty());
}
