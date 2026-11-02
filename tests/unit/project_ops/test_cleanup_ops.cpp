// SPDX-License-Identifier: Apache-2.0
// Opérations de nettoyage (image / segmentation / formes) par le dispatcher public.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include <string>
#include <utility>

#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/errors.hpp"
#include "openstitch/project_ops/operations.hpp"

using namespace openstitch;

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

// Segmentation 10x1 : rouge (6 px), quasi-rouge (1 px, îlot), bleu (3 px).
document::Project seg_project() {
    document::Project project;
    segmentation::Segmentation seg;
    seg.width = 10;
    seg.height = 1;
    seg.labels = {1, 1, 1, 2, 1, 1, 1, 3, 3, 3};
    seg.region_slots.push_back(segmentation::Region{RegionId{1}, {255, 0, 0}, 6});
    seg.region_slots.push_back(segmentation::Region{RegionId{2}, {250, 5, 5}, 1});
    seg.region_slots.push_back(segmentation::Region{RegionId{3}, {0, 0, 255}, 3});
    project.segmentation = std::move(seg);
    return project;
}

document::Project shape_project() {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "Forme";
    vec.paths = {rect(0, 0, 10000, 10000), rect(20000, 0, 20100, 100)};
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = "Tatami";
    emb.source_vector = vec.id;
    emb.params = document::TatamiParams{};
    project.embroidery_objects.push_back(emb);
    return project;
}

project_ops::BatchReport run(document::Project& project, const std::string& op) {
    return project_ops::apply_operation(project, op);
}

} // namespace

TEST_CASE("cleanup ops: seg_clean_islands absorbs the tiny island", "[project_ops][cleanup]") {
    auto project = seg_project();
    const auto report = run(project, R"({"op":"seg_clean_islands","min_px":2})");
    REQUIRE(report.ok);
    CHECK(project.segmentation->region_count() == 2);
    CHECK(project.segmentation->labels[3] == 1);
}

TEST_CASE("cleanup ops: seg_reduce_colors merges close hues and seg_merge_colors recolors",
          "[project_ops][cleanup]") {
    auto reduce = seg_project();
    REQUIRE(run(reduce, R"({"op":"seg_reduce_colors","max_distance":15})").ok);
    CHECK(reduce.segmentation->region_count() == 2);

    auto merge = seg_project();
    REQUIRE(
        run(merge,
            R"({"op":"seg_merge_colors","sources":["#0000FF"],"target":"#FF0000","merge_adjacent":false})")
            .ok);
    CHECK(merge.segmentation->find(RegionId{3})->rgb == std::array<std::uint8_t, 3>{255, 0, 0});

    auto unknown = seg_project();
    const auto bad =
        run(unknown, R"({"op":"seg_merge_colors","sources":["#123456"],"target":"#FF0000"})");
    CHECK_FALSE(bad.ok);
    CHECK(bad.outcomes.front().code == project_ops::code::kUnknownObject);
}

TEST_CASE("cleanup ops: region merge, removal and recolor with refusals",
          "[project_ops][cleanup]") {
    auto project = seg_project();
    REQUIRE(run(project, R"({"op":"seg_recolor_region","region":3,"rgb":"#00FF00"})").ok);
    CHECK(project.segmentation->find(RegionId{3})->rgb == std::array<std::uint8_t, 3>{0, 255, 0});
    REQUIRE(run(project, R"({"op":"seg_merge_regions","keep":1,"absorb":2})").ok);
    CHECK(project.segmentation->find(RegionId{2}) == nullptr);
    REQUIRE(run(project, R"({"op":"seg_remove_region","region":3})").ok);
    CHECK(project.segmentation->region_count() == 1);

    CHECK_FALSE(run(project, R"({"op":"seg_merge_regions","keep":1,"absorb":1})").ok);
    CHECK_FALSE(run(project, R"({"op":"seg_remove_region","region":99})").ok);
}

TEST_CASE("cleanup ops: segmentation ops refuse a project without segmentation",
          "[project_ops][cleanup]") {
    document::Project project;
    const auto report = run(project, R"({"op":"seg_clean_islands","min_px":5})");
    CHECK_FALSE(report.ok);
    CHECK(report.outcomes.front().code == project_ops::code::kRefused);
    CHECK_FALSE(run(project, R"({"op":"segment_image"})").ok);
    CHECK_FALSE(run(project, R"({"op":"image_denoise"})").ok);
}

TEST_CASE("cleanup ops: batch is transactional and a failing step leaves the project intact",
          "[project_ops][cleanup]") {
    auto project = seg_project();
    const auto before = project.segmentation->labels;
    const auto report =
        project_ops::apply_batch(project, R"({"schema":"openstitch.batch/1","operations":[
            {"op":"seg_clean_islands","min_px":2},
            {"op":"seg_remove_region","region":99}]})");
    CHECK_FALSE(report.ok);
    CHECK_FALSE(report.applied);
    CHECK(project.segmentation->labels == before);
}

TEST_CASE("cleanup ops: shape cleanup removes crumbs, simplifies, and refuses no-ops",
          "[project_ops][cleanup]") {
    auto project = shape_project();
    REQUIRE(run(project, R"({"op":"remove_small_pieces","min_area_mm2":0.5})").ok);
    CHECK(project.vector_objects.front().paths.size() == 1);
    CHECK_FALSE(run(project, R"({"op":"remove_small_pieces","min_area_mm2":0.5})").ok);
    CHECK_FALSE(run(project, R"({"op":"fill_small_holes"})").ok);                // aucun trou
    CHECK_FALSE(run(project, R"({"op":"smooth_shapes","tolerance_mm":50})").ok); // hors plage
    CHECK_FALSE(run(project, R"({"op":"simplify_shapes","objects":[99]})").ok);
}
