// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "openstitch/commands/arrange_ops.hpp"
#include "openstitch/commands/transform_ops.hpp"
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
    set.outer.closed = true;
    for (const Vec2um p : {pt(x0, y0), pt(x1, y0), pt(x1, y1), pt(x0, y1)}) {
        set.outer.nodes.push_back(geometry::PathNode{p, geometry::NodeType::Corner, {}, {}});
    }
    return set;
}

ObjectId add_shape(document::Project& project, geometry::PathSet shape) {
    document::VectorObject object;
    object.id = project.object_ids.next();
    object.paths = {std::move(shape)};
    project.vector_objects.push_back(object);
    return object.id;
}

std::int32_t min_x(const document::Project& project, ObjectId id) {
    return objects_bounds(project, {id})->min.x.value;
}
std::int32_t max_x(const document::Project& project, ObjectId id) {
    return objects_bounds(project, {id})->max.x.value;
}
std::int32_t min_y(const document::Project& project, ObjectId id) {
    return objects_bounds(project, {id})->min.y.value;
}

} // namespace

TEST_CASE("arrange: align left on the selection box moves the others to its left edge") {
    document::Project project;
    const ObjectId a = add_shape(project, rect(0, 0, 10'000, 10'000));
    const ObjectId b = add_shape(project, rect(5'000, 0, 25'000, 10'000));
    const auto shifts = align_shifts(project, {a, b}, AlignMode::Left, AlignReference::Selection);
    REQUIRE(shifts.size() == 1);
    CHECK(shifts[0].id == b);
    CHECK(shifts[0].delta == pt(-5'000, 0));
}

TEST_CASE("arrange: align to the first object keeps it fixed even when it is not extreme") {
    document::Project project;
    const ObjectId a = add_shape(project, rect(10'000, 0, 20'000, 10'000));
    const ObjectId b = add_shape(project, rect(0, 0, 4'000, 10'000));
    const ObjectId c = add_shape(project, rect(30'000, 0, 50'000, 10'000));
    UndoStack stack;
    stack.execute(make_shift_command(
                      align_shifts(project, {a, b, c}, AlignMode::Right, AlignReference::FirstObject),
                      "Aligner"),
                  project);
    CHECK(max_x(project, a) == 20'000);
    CHECK(max_x(project, b) == 20'000);
    CHECK(max_x(project, c) == 20'000);
    CHECK(stack.undo(project));
    CHECK(min_x(project, b) == 0);
    CHECK(min_x(project, c) == 30'000);
}

TEST_CASE("arrange: align to the frame uses the supplied box and Y up for top") {
    document::Project project;
    const ObjectId a = add_shape(project, rect(0, 0, 10'000, 4'000));
    BoundsUm frame;
    frame.min = pt(-50'000, -40'000);
    frame.max = pt(50'000, 40'000);
    auto top = align_shifts(project, {a}, AlignMode::Top, AlignReference::Frame, frame);
    REQUIRE(top.size() == 1);
    CHECK(top[0].delta == pt(0, 36'000));
    auto middle = align_shifts(project, {a}, AlignMode::CenterHorizontal, AlignReference::Frame,
                               frame);
    REQUIRE(middle.size() == 1);
    CHECK(middle[0].delta == pt(-5'000, 0));
    // Sans cadre fourni : retombe sur la sélection (une seule forme : rien à faire).
    CHECK(align_shifts(project, {a}, AlignMode::Top, AlignReference::Frame).empty());
}

TEST_CASE("arrange: distribute gives equal gaps and keeps both extremes") {
    document::Project project;
    const ObjectId a = add_shape(project, rect(0, 0, 10'000, 10'000));
    const ObjectId b = add_shape(project, rect(12'000, 0, 22'000, 10'000));
    const ObjectId c = add_shape(project, rect(90'000, 0, 100'000, 10'000));
    UndoStack stack;
    stack.execute(make_shift_command(distribute_shifts(project, {c, a, b}, true), "Repartir"),
                  project);
    CHECK(min_x(project, a) == 0);
    CHECK(min_x(project, c) == 90'000);
    // espace = (100000 - 30000) / 2 = 35000 -> b commence a 10000 + 35000
    CHECK(min_x(project, b) == 45'000);
    CHECK(max_x(project, b) - min_x(project, b) == 10'000);
    CHECK(stack.undo(project));
    CHECK(min_x(project, b) == 12'000);
}

TEST_CASE("arrange: distribute vertically and remainder is spread deterministically") {
    document::Project project;
    const ObjectId a = add_shape(project, rect(0, 0, 10, 10));
    const ObjectId b = add_shape(project, rect(0, 3, 10, 13));
    const ObjectId c = add_shape(project, rect(0, 6, 10, 16));
    const ObjectId d = add_shape(project, rect(0, 61, 10, 71));
    const auto shifts = distribute_shifts(project, {a, b, c, d}, false);
    UndoStack stack;
    stack.execute(make_shift_command(shifts, "Repartir"), project);
    // 31 de libre sur 3 espaces : 11, 10, 10 (reliquat aux premiers).
    CHECK(min_y(project, b) == 10 + 11);
    CHECK(min_y(project, c) == 10 + 11 + 10 + 10);
    CHECK(min_y(project, d) == 61);
    // Deux formes seulement : rien a repartir.
    CHECK(distribute_shifts(project, {a, d}, false).empty());
}

TEST_CASE("arrange: shape lock is one undo step and ignores shapes already in that state") {
    document::Project project;
    const ObjectId a = add_shape(project, rect(0, 0, 10, 10));
    const ObjectId b = add_shape(project, rect(0, 0, 10, 10));
    project.findObject(b)->locked = true;
    UndoStack stack;
    stack.execute(make_shape_lock_command(project, {a, b}, true), project);
    CHECK(project.findObject(a)->locked);
    CHECK(project.shapeEditLocked(a));
    CHECK(make_shape_lock_command(project, {a, b}, true) == nullptr);
    CHECK(stack.undo(project));
    CHECK_FALSE(project.findObject(a)->locked);
    CHECK(project.findObject(b)->locked); // etat precedent restitue
    CHECK_FALSE(stack.undo(project));
}

TEST_CASE("arrange: anisotropic scale moves dependent tatami rows and resizes the box") {
    document::Project project;
    const ObjectId a = add_shape(project, rect(0, 0, 10'000, 10'000));
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = a;
    document::TatamiParams tatami;
    tatami.angle = Angle{0.7853981633974483}; // 45 degres
    emb.params = tatami;
    project.embroidery_objects.push_back(emb);
    UndoStack stack;
    stack.execute(
        make_transform_command(project, {a}, AffineTransform::scale_xy(pt(0, 0), 2.0, 1.0), "T"),
        project);
    const auto box = objects_bounds(project, {a});
    CHECK(box->max.x.value == 20'000);
    CHECK(box->max.y.value == 10'000);
    const auto& turned = std::get<document::TatamiParams>(project.embroidery_objects[0].params);
    CHECK(turned.angle.radians < 0.7853981633974483); // les rangees s'aplatissent
    CHECK(stack.undo(project));
    CHECK(objects_bounds(project, {a})->max.x.value == 10'000);
}
