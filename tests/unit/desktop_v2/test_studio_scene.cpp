// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stop_token>

#include "document_session.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "simulation_nav.hpp"
#include "studio_camera.hpp"
#include "studio_scene.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

constexpr std::uint64_t firstId = (1ULL << 54) + 17;
constexpr std::uint64_t secondId = std::numeric_limits<std::uint64_t>::max() - 3;

document::Project runningProject() {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    for (const auto id : {firstId, secondId}) {
        document::VectorObject vector;
        vector.id = ObjectId{id - 1};
        geometry::Path path;
        path.closed = false;
        path.nodes = {{{Micrometers{-10'000}, Micrometers{5'000}}},
                      {{Micrometers{10'000}, Micrometers{5'000}}}};
        vector.paths.push_back({path, {}});
        project.vector_objects.push_back(vector);
        document::EmbroideryObject object;
        object.id = ObjectId{id};
        object.source_vector = vector.id;
        object.rgb = id == firstId ? std::array<std::uint8_t, 3>{180, 17, 65}
                                   : std::array<std::uint8_t, 3>{21, 180, 151};
        object.params = document::RunningStitchParams{Micrometers{2'000}};
        project.embroidery_objects.push_back(object);
    }
    project.object_ids.reset(secondId);
    return project;
}

stitch::StitchCommand command(std::int32_t x, std::int32_t y, stitch::CommandType type,
                              std::uint64_t source = 0) {
    return {{Micrometers{x}, Micrometers{y}}, type, ObjectId{source}};
}

} // namespace

TEST_CASE("studio camera round trips logical pixels and keeps the cursor anchored",
          "[desktop_v2][studio][camera]") {
    StudioCamera camera;
    camera.setViewport(1200, 800);
    camera.fit({-150, -100, 150, 100});
    const ScenePoint world{12.345, -67.89};
    const ScenePoint roundTrip = camera.screenToWorld(camera.worldToScreen(world));
    CHECK(roundTrip.x == Catch::Approx(world.x));
    CHECK(roundTrip.y == Catch::Approx(world.y));
    const ScenePoint cursor{231, 347};
    const auto anchor = camera.screenToWorld(cursor);
    camera.zoomAt(1.7, cursor);
    CHECK(camera.screenToWorld(cursor).x == Catch::Approx(anchor.x));
    CHECK(camera.screenToWorld(cursor).y == Catch::Approx(anchor.y));
    const auto beforePan = camera.worldToScreen(world);
    camera.pan(27, -13);
    CHECK(camera.worldToScreen(world).x == Catch::Approx(beforePan.x + 27));
    CHECK(camera.worldToScreen(world).y == Catch::Approx(beforePan.y - 13));
    const auto centre = camera.screenToWorld({600, 400});
    camera.setViewport(1400, 900);
    CHECK(camera.screenToWorld({700, 450}).x == Catch::Approx(centre.x));
    CHECK(camera.screenToWorld({700, 450}).y == Catch::Approx(centre.y));
}

TEST_CASE("studio camera fit respects margins and rejects invalid interaction values",
          "[desktop_v2][studio][camera]") {
    StudioCamera camera;
    camera.setViewport(1000, 700);
    camera.fit({-150, -100, 150, 100}, 40);
    const auto topLeft = camera.worldToScreen({-150, -100});
    const auto bottomRight = camera.worldToScreen({150, 100});
    constexpr double roundingTolerance = 1e-9;
    CHECK(topLeft.x >= 40 - roundingTolerance);
    CHECK(topLeft.y >= 40 - roundingTolerance);
    CHECK(bottomRight.x <= 960 + roundingTolerance);
    CHECK(bottomRight.y <= 660 + roundingTolerance);
    camera.zoomAt(1e20, {190, 280});
    const auto anchor = camera.screenToWorld({190, 280});
    camera.zoomAt(1e20, {190, 280}); // scale limit does not move the anchor
    CHECK(camera.screenToWorld({190, 280}) == anchor);
    const auto revision = camera.revision();
    camera.pan(std::nan(""), 1);
    camera.zoomAt(-1, {0, 0});
    camera.fit({0, 0, std::nan(""), 0});
    camera.setViewport(0, -1);
    CHECK(camera.revision() == revision);
    camera.fit({1, 2, 1, 2}); // a point/flat design still has a finite camera
    CHECK(std::isfinite(camera.scale()));
    CHECK(camera.worldToScreen({1, 2}).x == Catch::Approx(500));
    CHECK(camera.worldToScreen({1, 2}).y == Catch::Approx(350));
}

TEST_CASE("studio preserves sewing order thread colours and full 64 bit object identity",
          "[desktop_v2][studio][scene]") {
    const auto project = std::make_shared<const document::Project>(runningProject());
    const auto built = build_studio_scene(project, 42, TaskContext{});
    REQUIRE(built.has_value());
    const auto& scene = **built;
    CHECK(scene.project == project);
    CHECK(scene.revision == 42);
    CHECK(scene.timing.effectiveSequenceMs >= 0.0);
    CHECK(scene.timing.threadSegmentsMs >= 0.0);
    CHECK(scene.timing.verticesMs >= 0.0);
    REQUIRE(scene.effectiveSequence);
    const auto effective = stitch_generation::effective_sequence(*project);
    REQUIRE(effective.has_value());
    CHECK(scene.effectiveSequence->commands == effective->commands);
    const auto cachedMarks = simulationMarks(*scene.effectiveSequence);
    const auto regeneratedMarks = simulationMarks(*project);
    REQUIRE(regeneratedMarks.has_value());
    CHECK(cachedMarks.objects == regeneratedMarks->objects);
    CHECK(cachedMarks.colours == regeneratedMarks->colours);
    CHECK(cachedMarks.total == regeneratedMarks->total);
    REQUIRE_FALSE(scene.segments.empty());
    CHECK(scene.vertices.size() == scene.segments.size() * 6);
    CHECK(scene.objectBounds.size() == 2);
    CHECK(scene.segments.front().objectId == firstId);
    CHECK(scene.segments.back().objectId == secondId);
    CHECK(scene.pick(0, -5, 0.2) == secondId); // the later overlapping object wins
    CHECK_FALSE(scene.pick(0, 20, 0.2));
    CHECK_FALSE(scene.pick(0, -5, -1));
    REQUIRE(scene.pickGrid); // construit avec le snapshot, sur le worker
    CHECK(scene.pickGrid->segmentCount == scene.segments.size());
    CHECK(scene.timing.pickGridMs >= 0.0);
    CHECK(scene.boundsOf(firstId).has_value());
    CHECK_FALSE(scene.boundsOf(123));
    for (std::size_t i = 0; i < scene.segments.size(); ++i) {
        const auto& segment = scene.segments[i];
        const auto expected = segment.objectId == firstId ? project->embroidery_objects[0].rgb
                                                          : project->embroidery_objects[1].rgb;
        CHECK(segment.thread.rgb == expected);
        CHECK(segment.thread.ay == Catch::Approx(-5)); // document Y-up -> display Y-down
        for (std::size_t k = 0; k < 6; ++k) {
            const auto& vertex = scene.vertices[i * 6 + k];
            CHECK(vertex.r == expected[0]);
            CHECK(vertex.g == expected[1]);
            CHECK(vertex.b == expected[2]);
            CHECK(vertex.a == 255);
        }
    }
}

TEST_CASE("studio uses effective edited stitches and never changes the document or DST bytes",
          "[desktop_v2][studio][scene][export]") {
    auto project = runningProject();
    const auto initial = stitch_generation::effective_sequence(project);
    REQUIRE(initial.has_value());
    auto& object = project.embroidery_objects.front();
    const auto raw = stitch_generation::raw_slice(*initial, object.id);
    object.edited_fingerprint = stitch_generation::fingerprint(raw);
    object.edited_point_count = static_cast<std::uint32_t>(raw.size());
    const auto point = std::find_if(raw.begin(), raw.end(), [](const auto& entry) {
        return entry.type == stitch::CommandType::Stitch &&
               entry.pass == stitch::StitchPass::TopStitch;
    });
    REQUIRE(point != raw.end());
    document::StitchOverride edit;
    edit.base_index = static_cast<std::size_t>(std::distance(raw.begin(), point));
    edit.moved_to = point->pos + Vec2um{Micrometers{0}, Micrometers{2'000}};
    object.overrides.push_back(edit);

    DocumentSession session;
    session.replaceProject(std::move(project), true);
    const auto frozen = session.snapshot();
    const auto before = stitch_generation::effective_sequence(*frozen);
    REQUIRE(before.has_value());
    const auto dstBefore = formats::encode_dst(*before);
    REQUIRE(dstBefore.has_value());
    const auto revision = session.revision();
    const auto verticesBefore = frozen->vector_objects[0].paths;
    const auto scene = build_studio_scene(frozen, revision, TaskContext{});
    REQUIRE(scene.has_value());
    const auto expected =
        stitch_render::build_thread_segments(*before, before->commands.size(), [&](ObjectId id) {
            return std::optional(frozen->findEmbroidery(id)->rgb);
        });
    REQUIRE((*scene)->segments.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK((*scene)->segments[i].thread == expected[i]);
    }
    const auto after = stitch_generation::effective_sequence(session.project());
    REQUIRE(after.has_value());
    const auto dstAfter = formats::encode_dst(*after);
    REQUIRE(dstAfter.has_value());
    CHECK(*dstAfter == *dstBefore);
    CHECK(after->commands == before->commands);
    CHECK(session.snapshot() == frozen);
    CHECK(session.revision() == revision);
    CHECK_FALSE(session.dirty());
    CHECK_FALSE(session.canUndo());
    CHECK(session.project().vector_objects[0].paths == verticesBefore);
    CHECK(session.project().embroidery_objects[0].overrides == std::vector{edit});
}

TEST_CASE("studio omits hidden objects and hidden layers and clears an all hidden scene",
          "[desktop_v2][studio][scene]") {
    auto project = runningProject();
    project.embroidery_objects[0].visible = false;
    auto built = build_studio_scene(std::make_shared<const document::Project>(project), 1, {});
    REQUIRE(built.has_value());
    CHECK_FALSE((*built)->boundsOf(firstId));
    CHECK((*built)->boundsOf(secondId).has_value());
    project.calques.push_back({ObjectId{3}, "Hidden", false});
    project.vector_objects[1].calque = ObjectId{3};
    built = build_studio_scene(std::make_shared<const document::Project>(project), 2, {});
    REQUIRE(built.has_value());
    CHECK((*built)->segments.empty());
    CHECK((*built)->vertices.empty());
    CHECK((*built)->objectBounds.empty());
    CHECK_FALSE((*built)->hoop.empty());
}

TEST_CASE("studio follows shared jump trim stop and colour break semantics for imports",
          "[desktop_v2][studio][scene]") {
    using enum stitch::CommandType;
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    project.imported_design.emplace();
    auto& imported = *project.imported_design;
    imported.sequence.commands = {
        command(0, 0, Jump),        command(1'000, 0, Stitch),       command(10'000, 0, Jump),
        command(11'000, 0, Stitch), command(11'000, 0, Trim),        command(20'000, 0, Stitch),
        command(21'000, 0, Stitch), command(21'000, 0, Stop),        command(30'000, 0, Stitch),
        command(31'000, 0, Stitch), command(31'000, 0, ColorChange), command(40'000, 0, Stitch),
        command(41'000, 0, Stitch), command(41'000, 0, End)};
    imported.color_blocks = {{{180, 40, 80}, {}, 0, 11}, {{10, 180, 140}, {}, 11, 14}};
    const auto built =
        build_studio_scene(std::make_shared<const document::Project>(project), 3, {});
    REQUIRE(built.has_value());
    REQUIRE((*built)->segments.size() == 5);
    CHECK((*built)->segments[1].thread.ax == 10); // the jump itself is not a sewn segment
    CHECK((*built)->segments[1].thread.bx == 11);
    CHECK((*built)->segments[2].thread.ax == 20); // trim/stop reset continuity
    CHECK((*built)->segments[3].thread.ax == 30);
    CHECK((*built)->segments[4].thread.ax == 40);
    CHECK((*built)->segments[3].thread.rgb == imported.color_blocks[0].rgb);
    CHECK((*built)->segments[4].thread.rgb == imported.color_blocks[1].rgb);
    CHECK_FALSE((*built)->pick(0.5, 0, 0.1)); // imported source 0 has no editable object
}

TEST_CASE("studio resource invalidation separates geometry selection camera and context loss",
          "[desktop_v2][studio][cache]") {
    const auto project = std::make_shared<const document::Project>(runningProject());
    const auto first = build_studio_scene(project, 1, {});
    REQUIRE(first.has_value());
    StudioResourceState resources;
    CHECK(resources.observe(nullptr, 1, 1).geometry);
    CHECK_FALSE(resources.observe(nullptr, 1, 1).geometry);
    auto changed = resources.observe(*first, 1, 1);
    CHECK(changed.geometry);
    CHECK(changed.selection);
    CHECK_FALSE(changed.camera);
    changed = resources.observe(*first, 1, 1);
    CHECK_FALSE(changed.geometry);
    CHECK_FALSE(changed.selection);
    CHECK_FALSE(changed.camera);
    changed = resources.observe(*first, 2, 1);
    CHECK_FALSE(changed.geometry);
    CHECK(changed.selection);
    CHECK_FALSE(changed.camera);
    changed = resources.observe(*first, 2, 2);
    CHECK_FALSE(changed.geometry);
    CHECK_FALSE(changed.selection);
    CHECK(changed.camera);
    // Another document may have the same numeric revision: immutable identity also matters.
    const auto replacement = build_studio_scene(project, 1, {});
    REQUIRE(replacement.has_value());
    CHECK(resources.observe(*replacement, 2, 2).geometry);
    resources.reset();
    CHECK(resources.observe(*replacement, 2, 2).geometry);
    CHECK(resources.observe(nullptr, 2, 2).geometry); // releasing a scene clears GPU data
}

TEST_CASE("studio rejects missing projects and honours cancellation during geometry construction",
          "[desktop_v2][studio][scene]") {
    CHECK_FALSE(build_studio_scene(nullptr, 1, {}));
    auto project = std::make_shared<document::Project>();
    project->finishing = document::SequenceFinishing::legacy();
    project->imported_design.emplace();
    for (std::int32_t i = 0; i < 5'000; ++i) {
        project->imported_design->sequence.commands.push_back(
            command(i * 100, (i % 2) * 100, stitch::CommandType::Stitch));
    }
    CHECK_FALSE(build_studio_scene(project, 1, {}, 0));
    std::stop_source cancellation;
    TaskContext task;
    task.stop = cancellation.get_token();
    task.progress = [&](std::size_t done, std::size_t total) {
        if (done >= 1024 && total == 4999) {
            cancellation.request_stop();
        }
    };
    const auto result = build_studio_scene(project, 1, task);
    REQUIRE_FALSE(result.has_value());
    CHECK(cancellation.stop_requested());
    CHECK(result.error().category == ErrorCategory::Cancelled);
    const auto alreadyCancelled = build_studio_scene(project, 1, task);
    REQUIRE_FALSE(alreadyCancelled.has_value());
    CHECK(alreadyCancelled.error().category == ErrorCategory::Cancelled);
}

TEST_CASE("studio pick grid returns exactly what the linear scan returns", "[studio][pick]") {
    StudioSceneSnapshot linear;
    linear.threadWidthMm = 0.22;
    std::uint32_t state = 987654321;
    const auto next = [&state] {
        state = state * 1664525U + 1013904223U;
        return static_cast<double>(state >> 8) / static_cast<double>(1U << 24);
    };
    for (int i = 0; i < 6000; ++i) {
        const auto ax = static_cast<float>(next() * 120.0 - 60.0);
        const auto ay = static_cast<float>(next() * 90.0 - 45.0);
        // Segments courts, longs et diagonaux ; quelques-uns de longueur nulle.
        const double span = (i % 7 == 0) ? 40.0 : 3.0;
        const auto bx = (i % 11 == 0) ? ax : static_cast<float>(ax + (next() - 0.5) * span);
        const auto by = (i % 11 == 0) ? ay : static_cast<float>(ay + (next() - 0.5) * span);
        // Chevauchements (identifiants repris) et quelques segments sans identité (0).
        const std::uint64_t id = (i % 13 == 0) ? 0 : 1 + static_cast<std::uint64_t>(next() * 40);
        linear.segments.push_back({{ax, ay, bx, by, {1, 2, 3}}, id});
    }
    auto indexed = linear;
    indexed.pickGrid = build_pick_grid(indexed.segments);
    REQUIRE(indexed.pickGrid);

    std::size_t hits = 0;
    for (int i = 0; i < 20000; ++i) {
        const double x = next() * 200.0 - 100.0; // déborde de la scène des deux côtés
        const double y = next() * 160.0 - 80.0;
        static constexpr double tolerances[] = {0.0, 0.05, 0.5, 5.0, 60.0, 150.0};
        const double tolerance = tolerances[i % 6]; // 150 mm dépasse le plafond de cellules
        const auto expected = linear.pick(x, y, tolerance);
        CHECK(indexed.pick(x, y, tolerance) == expected);
        hits += expected ? 1 : 0;
    }
    CHECK(hits > 1000); // le test n'est pas vacueux : il y a des touches ET des manques

    // Invalides : mêmes refus que le balayage.
    CHECK_FALSE(indexed.pick(std::numeric_limits<double>::quiet_NaN(), 0, 1));
    CHECK_FALSE(indexed.pick(0, 0, -1));

    // Grille périmée (segments ajoutés après coup) : on retombe sur le balayage, résultat exact.
    auto stale = indexed;
    stale.segments.push_back({{500, 500, 501, 500, {1, 2, 3}}, 99});
    CHECK(stale.pick(500.5, 500, 0.1) == std::optional<std::uint64_t>(99));
}

TEST_CASE("studio pick grid copes with empty and unselectable scenes", "[studio][pick]") {
    StudioSceneSnapshot empty;
    empty.pickGrid = build_pick_grid(empty.segments);
    REQUIRE(empty.pickGrid);
    CHECK_FALSE(empty.pick(0, 0, 10));

    StudioSceneSnapshot anonymous;
    anonymous.segments.push_back({{0, 0, 1, 0, {1, 2, 3}}, 0});
    anonymous.pickGrid = build_pick_grid(anonymous.segments);
    CHECK_FALSE(anonymous.pick(0.5, 0, 1)); // objectId 0 n'est jamais sélectionnable

    StudioSceneSnapshot nonFinite;
    nonFinite.segments.push_back(
        {{std::numeric_limits<float>::quiet_NaN(), 0, 1, 0, {1, 2, 3}}, 7});
    nonFinite.segments.push_back({{0, 0, 1, 0, {1, 2, 3}}, 8});
    nonFinite.pickGrid = build_pick_grid(nonFinite.segments);
    CHECK(nonFinite.pick(0.5, 0, 0.1) == std::optional<std::uint64_t>(8));
}
