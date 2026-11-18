// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "shape_batch.hpp"
#include "shape_resolver.hpp"
#include "warp_brush.hpp"
#include <catch2/catch_test_macros.hpp>
using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
// Deux rectangles convertis ; le second perd ensuite sa broderie (forme non convertie).
struct Fixture {
    DocumentSession session;
    Target converted{};
    Target stitching{};
    Target bare{}; // forme sans broderie
};

void fill(Fixture& f) {
    auto empty = project_ops::create_empty_project({});
    REQUIRE(empty);
    f.session.replaceProject(std::move(*empty), true);
    REQUIRE(f.session.applyBatch(batchAddRect({0, 0}, 20, 10), "A").committed);
    REQUIRE(f.session.applyBatch(batchAddRect({40, 0}, 20, 10), "B").committed);
    const auto& project = f.session.project();
    REQUIRE(project.embroidery_objects.size() == 2);
    const auto first = project.embroidery_objects[0];
    const auto second = project.embroidery_objects[1];
    f.converted = {TargetKind::VectorObject, first.source_vector.value};
    f.stitching = {TargetKind::EmbroideryObject, first.id.value};
    f.bare = {TargetKind::VectorObject, second.source_vector.value};
    f.session.execute(std::make_unique<commands::RemoveEmbroideryObjectCommand>(second.id));
    REQUIRE(f.session.project().embroidery_objects.size() == 1);
}
} // namespace

TEST_CASE("shape resolver maps shapes and embroideries to the same shape", "[desktop_v2][shape]") {
    Fixture f;
    fill(f);
    const auto& project = f.session.project();
    CHECK(shapeOfTarget(project, f.stitching) == ObjectId{f.converted.id});
    CHECK(shapeOfTarget(project, f.converted) == ObjectId{f.converted.id});
    CHECK(shapeOfTarget(project, f.bare) == ObjectId{f.bare.id});
    CHECK_FALSE(shapeOfTarget(project, {TargetKind::Region, 1}));
    CHECK_FALSE(shapeOfTarget(project, {TargetKind::VectorObject, 999999}));
    CHECK_FALSE(shapeOfTarget(project, {TargetKind::EmbroideryObject, 999999}));

    // La forme et sa broderie ne comptent qu'une fois ; l'ordre de première apparition est gardé.
    const auto shapes = resolveEditableShapes(project, {f.stitching, f.bare, f.converted});
    REQUIRE(shapes.size() == 2);
    CHECK(shapes[0].value == f.converted.id);
    CHECK(shapes[1].value == f.bare.id);
}

TEST_CASE("shape removal takes the shape and its embroideries in one undo step",
          "[desktop_v2][shape]") {
    Fixture f;
    fill(f);
    const auto vectorsBefore = f.session.project().vector_objects.size();
    const auto depth = f.session.undoNames().size();

    auto removal = prepareShapeRemoval(f.session.project(), {f.stitching, f.bare});
    REQUIRE(removal.command);
    CHECK(removal.shapes == 2);
    CHECK(removal.embroideries == 1);
    f.session.execute(std::move(removal.command));

    CHECK(f.session.project().vector_objects.empty());
    CHECK(f.session.project().embroidery_objects.empty());
    CHECK(f.session.undoNames().size() == depth + 1);

    REQUIRE(f.session.undo());
    CHECK(f.session.project().vector_objects.size() == vectorsBefore);
    CHECK(f.session.project().embroidery_objects.size() == 1);
}

TEST_CASE("shape removal skips locked shapes and refuses an empty selection",
          "[desktop_v2][shape]") {
    Fixture f;
    fill(f);
    CHECK_FALSE(prepareShapeRemoval(f.session.project(), {}).command);
    CHECK_FALSE(prepareShapeRemoval(f.session.project(), {{TargetKind::Region, 1}}).command);

    auto locked = *f.session.snapshot();
    locked.findObject(ObjectId{f.bare.id})->locked = true;
    const auto removal = prepareShapeRemoval(locked, {f.bare});
    CHECK_FALSE(removal.command);
    CHECK(removal.shapes == 0);
}

TEST_CASE("warp brush works on a shape that has no embroidery", "[desktop_v2][shape][warp]") {
    Fixture f;
    fill(f);
    const auto& project = f.session.project();
    const std::vector<WarpTouch> touches{{Vec2um{Micrometers{40000}, Micrometers{5000}}, false},
                                         {Vec2um{Micrometers{40000}, Micrometers{7000}}, false}};
    auto preview = previewWarp(project, f.bare, {}, touches, true);
    REQUIRE(preview);
    CHECK(preview->paths != project.findObject(ObjectId{f.bare.id})->paths);
    REQUIRE(preview->command);

    const auto depth = f.session.undoNames().size();
    f.session.execute(std::move(preview->command));
    CHECK(f.session.undoNames().size() == depth + 1);
    CHECK(f.session.project().embroidery_objects.size() == 1); // aucune broderie créée

    // Même résultat que via la broderie pour une forme convertie.
    const auto& now = f.session.project();
    auto viaShape = previewWarp(now, f.converted, {}, touches, false);
    auto viaStitching = previewWarp(now, f.stitching, {}, touches, false);
    REQUIRE(viaShape);
    REQUIRE(viaStitching);
    CHECK(viaShape->paths == viaStitching->paths);

    CHECK_FALSE(previewWarp(now, {TargetKind::Region, 1}, {}, touches, false));
}
