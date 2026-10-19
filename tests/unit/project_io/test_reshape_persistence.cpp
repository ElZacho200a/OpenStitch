// SPDX-License-Identifier: Apache-2.0
// Remodelage interactif (HP-VEC-016/017/018) : une forme remodelée est sauvegardée en .osp puis
// rouverte sans perdre sa géométrie (tangentes comprises), et les points de broderie sont
// régénérés à l'identique à partir de la géométrie source (déterminisme).
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/commands/vector_path_commands.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
namespace fs = std::filesystem;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

fs::path temp_osp() {
    static int counter = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("openstitch_reshape_" + std::to_string(ticks) + "_" +
                                        std::to_string(counter++) + ".osp");
}

// Disque de 30 mm (24 nœuds) rempli en tatami + contour cousu, plus un second objet témoin.
document::Project project_with_shapes(ObjectId& target, ObjectId& witness) {
    document::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    for (const int cx : {0, 60'000}) {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.name = "forme";
        vec.rgb = {10, 120, 200};
        geometry::Path path;
        path.closed = true;
        for (int i = 0; i < 24; ++i) {
            const double a = 2.0 * 3.14159265358979 * i / 24.0;
            path.nodes.push_back({um(cx + static_cast<std::int32_t>(15'000 * std::cos(a)),
                                     static_cast<std::int32_t>(15'000 * std::sin(a))),
                                  geometry::NodeType::Corner, std::nullopt, std::nullopt});
        }
        vec.paths.push_back({path, {}});
        (cx == 0 ? target : witness) = vec.id;
        project.vector_objects.push_back(vec);

        document::EmbroideryObject fill;
        fill.id = project.object_ids.next();
        fill.name = "remplissage";
        fill.source_vector = vec.id;
        fill.rgb = vec.rgb;
        fill.params = document::TatamiParams{};
        project.embroidery_objects.push_back(fill);
    }
    return project;
}

} // namespace

TEST_CASE("a reshaped, node-transformed and brushed shape survives an osp round trip",
          "[project_io][reshape]") {
    ObjectId target;
    ObjectId witness;
    document::Project project = project_with_shapes(target, witness);
    const auto witnessBefore = project.findObject(witness)->paths;
    const auto paramsBefore = project.findEmbroidery(ObjectId{2})->params;

    commands::UndoStack stack;
    // 1. segment tiré (HP-VEC-016)
    auto reshape =
        commands::make_reshape_segment_command(project, target, 0, 0, 0, 0.5, um(2'000, 1'500));
    REQUIRE(reshape.command != nullptr);
    stack.execute(std::move(reshape.command), project);
    // 2. nœuds déplacés en groupe (HP-VEC-017)
    geometry::NodeTransform t;
    t.translation = um(800, -600);
    auto moved = commands::make_transform_nodes_command(project, target, 0, 0, {6, 7, 8}, t, 0.0);
    REQUIRE(moved.command != nullptr);
    stack.execute(std::move(moved.command), project);
    // 3. coup de pinceau (HP-VEC-018)
    geometry::BrushDab dab;
    dab.center = um(0, -15'000);
    dab.radius_um = 5'000.0;
    dab.delta = um(0, 1'200);
    auto brushed = commands::make_brush_stroke_command(
        project, target,
        {geometry::apply_brush_dab(project.findObject(target)->paths.front(), dab)});
    REQUIRE(brushed.command != nullptr);
    stack.execute(std::move(brushed.command), project);

    const auto shaped = project.findObject(target)->paths;
    CHECK(shaped != project_with_shapes(target, witness).findObject(target)->paths);
    // Le type de point et les réglages de broderie ne changent pas lors d'un remodelage.
    CHECK(project.findEmbroidery(ObjectId{2})->params == paramsBefore);
    // L'objet témoin n'est pas touché.
    CHECK(project.findObject(witness)->paths == witnessBefore);

    const fs::path path = temp_osp();
    REQUIRE(project_io::save_project(path, project));
    auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded);
    CHECK(loaded.value().findObject(target)->paths == shaped); // forme (et tangentes) intactes
    CHECK(loaded.value().findObject(witness)->paths == witnessBefore);

    // Les points sont recalculés depuis la géométrie source, identiques avant / après la
    // sauvegarde, et non vides.
    auto seqBefore = stitch_generation::effective_sequence(project);
    auto seqAfter = stitch_generation::effective_sequence(loaded.value());
    REQUIRE(seqBefore);
    REQUIRE(seqAfter);
    CHECK_FALSE(seqBefore.value().empty());
    CHECK(seqBefore.value().commands == seqAfter.value().commands);

    // L'annulation de tout le geste rend la forme d'origine, sans dérive.
    while (stack.canUndo()) {
        REQUIRE(stack.undo(project));
    }
    document::Project fresh = project_with_shapes(target, witness);
    CHECK(project.findObject(target)->paths == fresh.findObject(target)->paths);
}
