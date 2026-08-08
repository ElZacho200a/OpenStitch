// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/topology_cleanup.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace openstitch::ai_segmentation;
using openstitch::RegionId;
using openstitch::segmentation::Region;
using openstitch::segmentation::Segmentation;

namespace {

Segmentation makeSegmentation(int w, int h, std::vector<std::uint32_t> labels,
                              std::size_t regionCount) {
    Segmentation seg;
    seg.width = w;
    seg.height = h;
    seg.labels = std::move(labels);
    for (std::size_t i = 0; i < regionCount; ++i) {
        Region region;
        region.id = RegionId{i + 1};
        region.rgb = {0, 0, 0};
        seg.region_slots.push_back(region);
    }
    return seg;
}

}  // namespace

TEST_CASE("cleanup_topology absorbs a tiny isolated island into its only neighbor") {
    // 5x5 : tout en label 1, sauf un pixel isole en label 2 au centre.
    constexpr int w = 5;
    constexpr int h = 5;
    std::vector<std::uint32_t> labels(static_cast<std::size_t>(w * h), 1);
    labels[2 * w + 2] = 2;  // (2,2)
    Segmentation seg = makeSegmentation(w, h, labels, 2);

    TopologyCleanupOptions options;
    options.mm_per_px = 1.0;
    options.min_island_area_mm2 = 2.0;
    options.min_hole_area_mm2 = 2.0;
    options.thin_band_max_width_mm = 0.01;

    const auto result = cleanup_topology(seg, options);
    REQUIRE(result.has_value());
    CHECK(result->tiny_island_count == 1);
    CHECK(seg.labels[2 * w + 2] == 1);        // absorbe dans la seule region voisine
    CHECK(seg.find(RegionId{2}) == nullptr);  // slot purge (0 pixel restant)
    CHECK(seg.find(RegionId{1})->pixel_count == 25);
    CHECK(result->label_count == 1);
    CHECK(result->ready_for_vectorization);
}

TEST_CASE("cleanup_topology fills a tiny fully-enclosed hole") {
    constexpr int w = 5;
    constexpr int h = 5;
    std::vector<std::uint32_t> labels(static_cast<std::size_t>(w * h), 1);
    labels[1 * w + 1] = 0;  // (1,1) : trou entoure de label 1 de toutes parts
    Segmentation seg = makeSegmentation(w, h, labels, 1);

    TopologyCleanupOptions options;
    options.mm_per_px = 1.0;
    options.min_island_area_mm2 = 2.0;
    options.min_hole_area_mm2 = 2.0;
    options.thin_band_max_width_mm = 0.01;

    const auto result = cleanup_topology(seg, options);
    REQUIRE(result.has_value());
    CHECK(result->tiny_hole_count == 1);
    CHECK(result->ambiguous_component_count == 0);
    CHECK(seg.labels[1 * w + 1] == 1);
    CHECK(seg.find(RegionId{1})->pixel_count == 25);
}

TEST_CASE("cleanup_topology reports an ambiguous hole touching two different regions") {
    // 3x3 : colonne 2 = label 2, reste = label 1, sauf (1,1) = trou touchant
    // trois cotes label 1 et un cote label 2.
    constexpr int w = 3;
    constexpr int h = 3;
    std::vector<std::uint32_t> labels = {
        1, 1, 2,
        1, 0, 2,
        1, 1, 2,
    };
    Segmentation seg = makeSegmentation(w, h, labels, 2);

    TopologyCleanupOptions options;
    options.mm_per_px = 1.0;
    options.min_island_area_mm2 = 0.0;  // desactive la logique d'ilot pour ce test
    options.min_hole_area_mm2 = 2.0;
    options.thin_band_max_width_mm = 0.01;

    const auto result = cleanup_topology(seg, options);
    REQUIRE(result.has_value());
    CHECK(result->tiny_hole_count == 1);
    CHECK(result->ambiguous_component_count == 1);
    CHECK(seg.labels[1 * w + 1] == 1);  // majoritaire (3 aretes contre 1)
}

TEST_CASE("cleanup_topology never merges or removes a protected region") {
    constexpr int w = 5;
    constexpr int h = 5;
    std::vector<std::uint32_t> labels(static_cast<std::size_t>(w * h), 1);
    labels[2 * w + 2] = 2;  // meme configuration que le premier test...
    Segmentation seg = makeSegmentation(w, h, labels, 2);

    TopologyCleanupOptions options;
    options.mm_per_px = 1.0;
    options.min_island_area_mm2 = 2.0;
    options.min_hole_area_mm2 = 2.0;
    options.thin_band_max_width_mm = 0.01;
    options.protected_regions = {RegionId{2}};  // ...mais region 2 est protegee

    const auto result = cleanup_topology(seg, options);
    REQUIRE(result.has_value());
    CHECK(result->tiny_island_count == 1);       // toujours signalee comme fine...
    CHECK(seg.labels[2 * w + 2] == 2);            // ...mais jamais fusionnee
    CHECK(seg.find(RegionId{2}) != nullptr);
    CHECK(result->label_count == 2);
}

TEST_CASE("cleanup_topology rejects an empty segmentation") {
    Segmentation seg;
    const auto result = cleanup_topology(seg, TopologyCleanupOptions{});
    CHECK_FALSE(result.has_value());
}
