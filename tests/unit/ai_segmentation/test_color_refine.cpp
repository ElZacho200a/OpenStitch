// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/color_refine.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace openstitch::ai_segmentation;
using openstitch::RegionId;
using openstitch::image::Image;
using openstitch::segmentation::Region;
using openstitch::segmentation::Segmentation;

namespace {

// 4x4. Colonnes 0-1 = region AI 1 (rouge en haut, bleu en bas -- deux
// couleurs nettes) ; colonnes 2-3 = region AI 2 (vert uni, une seule
// couleur). Aucun fond (toute la grille appartient a une region AI).
Segmentation twoRegionLabelMap() {
    Segmentation seg;
    seg.width = 4;
    seg.height = 4;
    seg.labels.assign(16, 0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            seg.labels[static_cast<std::size_t>(y) * 4 + static_cast<std::size_t>(x)] = (x < 2) ? 1 : 2;
        }
    }
    Region region1;
    region1.id = RegionId{1};
    seg.region_slots.push_back(region1);
    Region region2;
    region2.id = RegionId{2};
    seg.region_slots.push_back(region2);
    return seg;
}

Image colorSourceImage() {
    Image img;
    img.width = 4;
    img.height = 4;
    img.source_had_alpha = false;
    img.rgba.assign(static_cast<std::size_t>(4 * 4 * 4), 255);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * 4 + static_cast<std::size_t>(x)) * 4;
            std::uint8_t r = 0, g = 0, b = 0;
            if (x < 2) {
                if (y < 2) {
                    r = 220;  // rouge (haut de la region 1)
                } else {
                    b = 220;  // bleu (bas de la region 1)
                }
            } else {
                g = 220;  // vert uni (region 2 entiere)
            }
            img.rgba[idx + 0] = r;
            img.rgba[idx + 1] = g;
            img.rgba[idx + 2] = b;
            img.rgba[idx + 3] = 255;
        }
    }
    return img;
}

}  // namespace

TEST_CASE("refine_label_map_by_color splits a region with two distinct colors in two") {
    const Segmentation labelMap = twoRegionLabelMap();
    const Image source = colorSourceImage();

    const auto result = refine_label_map_by_color(labelMap, source, ColorRefineOptions{2, 1});
    REQUIRE(result.has_value());

    // Region 1 (rouge/bleu) -> 2 sous-regions ; region 2 (vert uni) -> 1.
    REQUIRE(result->region_count() == 3);

    const auto labelAt = [&](int x, int y) {
        return result->labels[static_cast<std::size_t>(y) * 4 + static_cast<std::size_t>(x)];
    };
    // Le haut et le bas de la region 1 doivent porter des labels differents...
    CHECK(labelAt(0, 0) != labelAt(0, 3));
    CHECK(labelAt(0, 0) == labelAt(1, 1));  // uniforme a l'interieur du rouge
    CHECK(labelAt(0, 3) == labelAt(1, 2));  // uniforme a l'interieur du bleu
    // ...et toute la region 2 (verte) doit porter UN SEUL label, distinct des deux precedents.
    CHECK(labelAt(2, 0) == labelAt(3, 3));
    CHECK(labelAt(2, 0) != labelAt(0, 0));
    CHECK(labelAt(2, 0) != labelAt(0, 3));
}

TEST_CASE("refine_label_map_by_color leaves background pixels as background") {
    Segmentation labelMap;
    labelMap.width = 4;
    labelMap.height = 4;
    labelMap.labels.assign(16, 0);
    // Seule la colonne 0 appartient a une region AI ; le reste est fond.
    for (int y = 0; y < 4; ++y) {
        labelMap.labels[static_cast<std::size_t>(y) * 4] = 1;
    }
    Region region;
    region.id = RegionId{1};
    labelMap.region_slots.push_back(region);

    const Image source = colorSourceImage();
    const auto result = refine_label_map_by_color(labelMap, source, ColorRefineOptions{2, 1});
    REQUIRE(result.has_value());
    for (int y = 0; y < 4; ++y) {
        for (int x = 1; x < 4; ++x) {
            CHECK(result->labels[static_cast<std::size_t>(y) * 4 + static_cast<std::size_t>(x)] == 0);
        }
    }
}

TEST_CASE("refine_label_map_by_color rejects mismatched dimensions") {
    Segmentation labelMap;
    labelMap.width = 4;
    labelMap.height = 4;
    labelMap.labels.assign(16, 0);

    Image source;
    source.width = 8;
    source.height = 8;
    source.rgba.assign(static_cast<std::size_t>(8 * 8 * 4), 0);

    const auto result = refine_label_map_by_color(labelMap, source, ColorRefineOptions{});
    CHECK_FALSE(result.has_value());
}

TEST_CASE("refine_label_map_by_color fails cleanly when the label map has no live region") {
    Segmentation labelMap;
    labelMap.width = 2;
    labelMap.height = 2;
    labelMap.labels.assign(4, 0);

    Image source;
    source.width = 2;
    source.height = 2;
    source.rgba.assign(static_cast<std::size_t>(2 * 2 * 4), 0);

    const auto result = refine_label_map_by_color(labelMap, source, ColorRefineOptions{});
    CHECK_FALSE(result.has_value());
}
