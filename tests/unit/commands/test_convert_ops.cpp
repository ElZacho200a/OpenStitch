// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <variant>
#include <vector>

#include "openstitch/commands/convert_ops.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"

using namespace openstitch;
using namespace openstitch::commands;

namespace {

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

geometry::Path line(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    geometry::Path path;
    path.closed = false;
    path.nodes.push_back(geometry::PathNode{pt(x0, y0), geometry::NodeType::Corner, {}, {}});
    path.nodes.push_back(geometry::PathNode{pt(x1, y1), geometry::NodeType::Corner, {}, {}});
    return path;
}

geometry::PathSet rect(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    geometry::PathSet set;
    set.outer.closed = true;
    for (const Vec2um p : {pt(x0, y0), pt(x1, y0), pt(x1, y1), pt(x0, y1)}) {
        set.outer.nodes.push_back(geometry::PathNode{p, geometry::NodeType::Corner, {}, {}});
    }
    return set;
}

// Colonne satin manuelle : forme source cachee (proxy), deux rails horizontaux.
ObjectId add_manual_satin(document::Project& project) {
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.visible = false;
    vec.paths = {};
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = "colonne";
    emb.source_vector = vec.id;
    emb.rgb = {10, 20, 30};
    document::SatinParams satin;
    satin.rail_a = line(0, 0, 20'000, 0);
    satin.rail_b = line(0, 4'000, 20'000, 4'000);
    satin.entry_point = pt(1'000, 2'000);
    emb.params = satin;
    project.embroidery_objects.push_back(emb);
    return emb.id;
}

ObjectId add_filled_rect(document::Project& project, document::StitchParams params) {
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths = {rect(0, 0, 10'000, 10'000)};
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.rgb = {200, 100, 50};
    emb.params = std::move(params);
    project.embroidery_objects.push_back(emb);
    return emb.id;
}

} // namespace

TEST_CASE("convert: manual satin becomes a running stitch on its centre line and undoes exactly") {
    document::Project project;
    const ObjectId id = add_manual_satin(project);
    const auto before = project.embroidery_objects[0];
    const auto vectorBefore = project.vector_objects[0];
    auto plan = make_conversion_command(project, id, ConversionKind::SatinToCenterline);
    REQUIRE(plan.command != nullptr);
    UndoStack stack;
    stack.execute(std::move(plan.command), project);

    const auto& emb = project.embroidery_objects[0];
    CHECK(std::holds_alternative<document::RunningStitchParams>(emb.params));
    CHECK(emb.rgb == before.rgb);
    CHECK(emb.name == before.name);
    const auto& vec = project.vector_objects[0];
    CHECK(vec.visible);
    REQUIRE(vec.paths.size() == 1);
    CHECK_FALSE(vec.paths[0].outer.closed);
    for (const auto& node : vec.paths[0].outer.nodes) {
        CHECK(node.pos.y.value == 2'000); // a mi-chemin des rails
    }
    CHECK(vec.paths[0].outer.nodes.front().pos.x.value == 0);
    CHECK(vec.paths[0].outer.nodes.back().pos.x.value == 20'000);

    CHECK(stack.undo(project));
    CHECK(project.embroidery_objects[0].params == before.params);
    CHECK(project.vector_objects[0].paths == vectorBefore.paths);
    CHECK_FALSE(project.vector_objects[0].visible);
}

TEST_CASE("convert: satin to filled shape keeps thread and entry point") {
    document::Project project;
    const ObjectId id = add_manual_satin(project);
    auto plan = make_conversion_command(project, id, ConversionKind::SatinToFilledShape);
    REQUIRE(plan.command != nullptr);
    UndoStack stack;
    stack.execute(std::move(plan.command), project);
    const auto& emb = project.embroidery_objects[0];
    const auto* tatami = std::get_if<document::TatamiParams>(&emb.params);
    REQUIRE(tatami != nullptr);
    CHECK(tatami->entry_point == pt(1'000, 2'000));
    CHECK(emb.rgb == std::array<std::uint8_t, 3>{10, 20, 30});
    const auto& outline = project.vector_objects[0].paths.at(0).outer;
    CHECK(outline.closed);
    CHECK(outline.nodes.size() == 4);
    CHECK(stack.undo(project));
    CHECK(std::holds_alternative<document::SatinParams>(project.embroidery_objects[0].params));
}

TEST_CASE("convert: running contour becomes a border satin on the same outline") {
    document::Project project;
    const ObjectId id = add_filled_rect(project, document::RunningStitchParams{});
    auto plan = make_conversion_command(project, id, ConversionKind::ContourToBorderSatin,
                                        Micrometers{2'000});
    REQUIRE(plan.command != nullptr);
    UndoStack stack;
    stack.execute(std::move(plan.command), project);
    REQUIRE(project.embroidery_objects.size() == 1);
    const auto* satin = std::get_if<document::SatinParams>(&project.embroidery_objects[0].params);
    REQUIRE(satin != nullptr);
    REQUIRE(satin->border.has_value());
    CHECK(satin->border->width.value == 2'000);
    CHECK(project.embroidery_objects[0].rgb == std::array<std::uint8_t, 3>{200, 100, 50});
    CHECK(stack.undo(project));
    CHECK(std::holds_alternative<document::RunningStitchParams>(project.embroidery_objects[0].params));
}

TEST_CASE("convert: tatami becomes an edge contour and the other conversions refuse it") {
    document::Project project;
    const ObjectId id = add_filled_rect(project, document::TatamiParams{});
    CHECK(conversion_applicable(project.embroidery_objects[0], ConversionKind::FillToContour));
    CHECK_FALSE(conversion_applicable(project.embroidery_objects[0],
                                      ConversionKind::SatinToCenterline));
    auto refused = make_conversion_command(project, id, ConversionKind::SatinToFilledShape);
    CHECK(refused.command == nullptr);
    CHECK_FALSE(refused.error.empty());
    auto plan = make_conversion_command(project, id, ConversionKind::FillToContour);
    REQUIRE(plan.command != nullptr);
    UndoStack stack;
    stack.execute(std::move(plan.command), project);
    CHECK(std::holds_alternative<document::RunningStitchParams>(project.embroidery_objects[0].params));
    CHECK(project.vector_objects[0].paths.size() == 1); // le bord de la forme reste la source
}
