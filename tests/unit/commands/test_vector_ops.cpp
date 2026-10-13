// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>
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

TEST_CASE("paste: copies get new ids, keep their embroidery and undo in one step") {
    document::Project project;
    const ObjectId a = add_object(project, rect(0, 0, 10'000, 10'000), true);
    const ObjectId b = add_object(project, rect(20'000, 0, 30'000, 10'000), false);
    (void)b;
    // Directionnel avec un guide : il doit suivre le decalage.
    auto& emb = project.embroidery_objects[0];
    document::DirectionalFillParams dir;
    geometry::Path guide;
    guide.nodes = {{pt(1'000, 5'000), geometry::NodeType::Corner, {}, {}},
                   {pt(9'000, 5'000), geometry::NodeType::Corner, {}, {}}};
    dir.guides.push_back(guide);
    emb.params = dir;
    emb.overrides.push_back({});

    // Collecte : doublons et identifiant inconnu ignores, broderie dependante incluse.
    const auto fragment = collect_fragment(project, {a, a, ObjectId{999'999}});
    REQUIRE(fragment.vectors.size() == 1);
    REQUIRE(fragment.embroideries.size() == 1);
    CHECK(project.vector_objects.size() == 2); // document intact

    UndoStack stack;
    auto pasted = make_paste_command(project, fragment, pt(5'000, -5'000));
    REQUIRE(pasted.command != nullptr);
    REQUIRE(pasted.pasted.size() == 1);
    stack.execute(std::move(pasted.command), project);

    REQUIRE(project.vector_objects.size() == 3);
    REQUIRE(project.embroidery_objects.size() == 2);
    const auto& copy = project.vector_objects.back();
    CHECK(copy.id == pasted.pasted[0]);
    CHECK(copy.id != a);
    CHECK(copy.paths[0].outer.nodes[0].pos == pt(5'000, -5'000));
    const auto& copyEmb = project.embroidery_objects.back();
    CHECK(copyEmb.id != project.embroidery_objects[0].id);
    CHECK(copyEmb.source_vector == copy.id);
    const auto& copyDir = std::get<document::DirectionalFillParams>(copyEmb.params);
    CHECK(copyDir.guides[0].nodes[0].pos == pt(6'000, 0));
    CHECK(copyEmb.overrides.empty()); // collage decale : retouches retirees
    // L'original n'a pas bouge.
    CHECK(project.vector_objects[0].paths[0].outer.nodes[0].pos == pt(0, 0));
    CHECK(project.embroidery_objects[0].overrides.size() == 1);

    stack.undo(project);
    CHECK(project.vector_objects.size() == 2);
    CHECK(project.embroidery_objects.size() == 1);

    // Collage sur place : memes positions, retouches conservees.
    auto inPlace = make_paste_command(project, fragment, pt(0, 0));
    stack.execute(std::move(inPlace.command), project);
    CHECK(project.vector_objects.back().paths[0].outer.nodes[0].pos == pt(0, 0));
    CHECK(project.embroidery_objects.back().overrides.size() == 1);

    // Fragment vide : aucune commande.
    CHECK(make_paste_command(project, {}, pt(0, 0)).command == nullptr);
}

TEST_CASE("paste: text letters and foreign regions become plain shapes") {
    document::Project project;
    const ObjectId a = add_object(project, rect(0, 0, 10'000, 10'000), false);
    project.vector_objects[0].text_owner = ObjectId{42};
    project.vector_objects[0].source_region = RegionId{7}; // aucune segmentation dans la cible
    auto pasted = make_paste_command(project, collect_fragment(project, {a}), pt(0, 0));
    UndoStack stack;
    stack.execute(std::move(pasted.command), project);
    CHECK_FALSE(project.vector_objects.back().text_owner.has_value());
    CHECK_FALSE(project.vector_objects.back().source_region.has_value());
}

TEST_CASE("vector ops: exclude keeps what belongs to exactly one shape") {
    document::Project project;
    const ObjectId a = add_object(project, rect(0, 0, 10'000, 10'000), true);
    const ObjectId b = add_object(project, rect(5'000, 0, 15'000, 10'000), false);
    UndoStack stack;
    auto result = make_boolean_command(project, BooleanOp::Exclude, {a, b});
    REQUIRE(result.command != nullptr);
    stack.execute(std::move(result.command), project);
    REQUIRE(project.vector_objects.size() == 1);
    CHECK(project.vector_objects[0].id == b);
    // 150 mm2 d'union moins 50 mm2 communs = 100 mm2, en deux morceaux.
    CHECK(project.vector_objects[0].paths.size() == 2);
    CHECK(std::abs(area_mm2(project.vector_objects[0]) - 100.0) < 0.5);
    stack.undo(project);
    CHECK(project.vector_objects.size() == 2);
}

TEST_CASE("vector ops: holes are removed, all or only the small ones") {
    document::Project project;
    geometry::PathSet shape = rect(0, 0, 40'000, 40'000);
    shape.holes.push_back(rect(2'000, 2'000, 4'000, 4'000).outer);     // 4 mm2
    shape.holes.push_back(rect(10'000, 10'000, 30'000, 30'000).outer); // 400 mm2
    const ObjectId id = add_object(project, shape, false);
    const ObjectId solid = add_object(project, rect(50'000, 0, 60'000, 10'000), false);

    UndoStack stack;
    // Seuil 10 mm2 : seul le petit trou disparait.
    auto small = make_remove_holes_command(project, {id, solid}, 10.0 * 1e6);
    REQUIRE(small.command != nullptr);
    CHECK(small.resulting_objects == 1);
    stack.execute(std::move(small.command), project);
    REQUIRE(project.vector_objects[0].paths[0].holes.size() == 1);
    CHECK(std::abs(area_mm2(project.vector_objects[0]) - 1200.0) < 0.5);

    auto all = make_remove_holes_command(project, {id});
    REQUIRE(all.command != nullptr);
    stack.execute(std::move(all.command), project);
    CHECK(project.vector_objects[0].paths[0].holes.empty());

    // Plus rien a retirer : refuse avec un message.
    const auto none = make_remove_holes_command(project, {id, solid});
    CHECK(none.command == nullptr);
    CHECK_FALSE(none.error.empty());

    stack.undo(project);
    stack.undo(project);
    CHECK(project.vector_objects[0].paths[0].holes.size() == 2);
}
