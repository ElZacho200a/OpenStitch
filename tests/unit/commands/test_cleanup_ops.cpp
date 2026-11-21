// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "openstitch/commands/cleanup_ops.hpp"
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
    const ObjectId id = add_object(project, {rect(0, 0, 10000, 10000), rect(20000, 0, 20100, 100),
                                             rect(30000, 0, 30100, 100)});
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
    const ObjectId big =
        add_object(project, {rect(0, 0, 10000, 10000), rect(20000, 0, 30000, 10000)});
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
    CHECK_FALSE(
        make_reshape_paths_command(project, {id}, ReshapeKind::Smooth, Micrometers{0}).command);
    project.findObject(id)->locked = true;
    CHECK_FALSE(
        make_reshape_paths_command(project, {id}, ReshapeKind::Simplify, Micrometers{50}).command);
}

namespace {

double area_mm2(const document::Project& project, ObjectId id) {
    double area = 0.0;
    for (const auto& set : project.findObject(id)->paths) {
        area += geometry::path_set_area_um2(set);
    }
    return area / 1e6;
}

ObjectId add_colored(document::Project& project, geometry::PathSet shape,
                     std::array<std::uint8_t, 3> rgb) {
    const ObjectId id = add_object(project, {std::move(shape)});
    project.findObject(id)->rgb = rgb;
    return id;
}

constexpr std::array<std::uint8_t, 3> kRed{200, 20, 30};
constexpr std::array<std::uint8_t, 3> kBlue{20, 40, 200};

} // namespace

TEST_CASE("cleanup ops: absorb crumbs merges them into a neighbour without a hole",
          "[cleanup_ops][crumbs]") {
    document::Project project;
    const ObjectId red = add_colored(project, rect(0, 0, 10000, 10000), kRed);
    const ObjectId blue = add_colored(project, rect(10100, 0, 20000, 10000), kBlue);
    // Miette rouge à 0,1 mm du bleu seulement, miette rouge à 0,1 mm du rouge.
    const ObjectId nearBlue = add_colored(project, rect(20100, 0, 21100, 1000), kRed);
    const ObjectId nearRed = add_colored(project, rect(4000, 10100, 5000, 11100), kRed);
    const double redBefore = area_mm2(project, red);
    const double blueBefore = area_mm2(project, blue);

    UndoStack stack;
    auto made = make_absorb_small_objects_command(project, {nearBlue, nearRed},
                                                  AbsorbPriority::SameThread, Micrometers{200});
    REQUIRE(made.command);
    CHECK(made.resulting_objects == 2);
    stack.execute(std::move(made.command), project);
    CHECK(project.findObject(nearBlue) == nullptr);
    CHECK(project.findObject(nearRed) == nullptr);
    CHECK(area_mm2(project, red) >= redBefore + 1.0);   // même fil
    CHECK(area_mm2(project, blue) >= blueBefore + 1.0); // seul voisin à portée
    CHECK(project.findObject(red)->paths.size() == 1);  // soudée, pas un morceau à part
    stack.undo(project);
    CHECK(project.findObject(nearBlue) != nullptr);
    CHECK(area_mm2(project, red) == redBefore);

    // Hors de portée : rien à faire, refus montrable.
    const ObjectId far = add_colored(project, rect(50000, 50000, 51000, 51000), kRed);
    auto none = make_absorb_small_objects_command(project, {far}, AbsorbPriority::Largest,
                                                  Micrometers{200});
    CHECK_FALSE(none.command);
    CHECK_FALSE(none.error.empty());
}

TEST_CASE("cleanup ops: fill gaps between shapes keeps a shape's own holes",
          "[cleanup_ops][gaps]") {
    document::Project project;
    // Quatre formes autour d'un vide fermé de 1 mm² (5..6 x 4..5 mm).
    add_object(project, {rect(0, 0, 5000, 10000)});
    add_object(project, {rect(6000, 0, 11000, 10000)});
    add_object(project, {rect(5000, 0, 6000, 4000)});
    add_object(project, {rect(5000, 5000, 6000, 10000)});
    // Anneau isolé : son trou de 1 mm² est voulu.
    auto ring = rect(20000, 0, 30000, 10000);
    ring.holes.push_back(rect(24000, 4000, 25000, 5000).outer);
    const ObjectId ringId = add_object(project, {ring});

    UndoStack stack;
    auto made = make_fill_gaps_between_command(project, 4.0e6, Micrometers{200});
    REQUIRE(made.command);
    CHECK(made.resulting_objects == 1);
    stack.execute(std::move(made.command), project);
    CHECK(project.findObject(ringId)->paths.front().holes.size() == 1);
    double after = 0.0;
    for (const auto& shape : project.vector_objects) {
        after += area_mm2(project, shape.id);
    }
    CHECK(after == Catch::Approx(50.0 + 50.0 + 4.0 + 5.0 + 99.0 + 1.0).margin(0.01));
    stack.undo(project);

    // Seuil sous l'aire du vide : refus.
    CHECK_FALSE(make_fill_gaps_between_command(project, 0.5e6, Micrometers{200}).command);
}

TEST_CASE("cleanup ops: union welds shapes separated by a thin slit", "[cleanup_ops][weld]") {
    document::Project project;
    const ObjectId a = add_object(project, {rect(0, 0, 10000, 10000)});
    const ObjectId b = add_object(project, {rect(10200, 0, 20000, 10000)}); // fente 0,2 mm
    auto strict = make_boolean_command(project, BooleanOp::Union, {a, b}, Micrometers{50});
    REQUIRE(strict.command);
    UndoStack stack;
    stack.execute(std::move(strict.command), project);
    CHECK(project.findObject(b)->paths.size() == 2); // 0,05 mm ne soude pas : deux morceaux
    stack.undo(project);
    auto welded = make_boolean_command(project, BooleanOp::Union, {a, b}, Micrometers{300});
    REQUIRE(welded.command);
    stack.execute(std::move(welded.command), project);
    REQUIRE(project.findObject(b)->paths.size() == 1);
    // La matière d'origine reste entière (les angles ne sont pas rognés).
    CHECK(area_mm2(project, b) >= 100.0 + 98.0);
}
