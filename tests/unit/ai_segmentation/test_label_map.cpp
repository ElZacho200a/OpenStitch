// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/label_map.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace openstitch::ai_segmentation;
using openstitch::RegionId;
using openstitch::segmentation::Segmentation;

namespace {

// Grille 4x4. Masque A couvre la colonne 0..2 (3x4 = 12 px), masque B couvre
// la colonne 2..3 (2x4 = 8 px) : elles se recouvrent sur la colonne 2 (4 px).
std::vector<std::uint8_t> columnMask(int width, int height, int fromCol, int toColExclusive) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = fromCol; x < toColExclusive; ++x) {
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = 1;
        }
    }
    return pixels;
}

}  // namespace

TEST_CASE("build_label_map partitions overlapping masks with no overlap in the result") {
    constexpr int w = 4;
    constexpr int h = 4;
    LabelMaskInput a;
    a.mask_id = 0;
    a.pixels = columnMask(w, h, 0, 3);  // colonnes 0,1,2
    a.rgb = {255, 0, 0};
    a.stability_score = 0.9;
    a.predicted_iou = 0.9;

    LabelMaskInput b;
    b.mask_id = 1;
    b.pixels = columnMask(w, h, 2, 4);  // colonnes 2,3 -- chevauche a sur la colonne 2
    b.rgb = {0, 255, 0};
    b.stability_score = 0.5;  // moins prioritaire que a
    b.predicted_iou = 0.5;

    const auto result = build_label_map({a, b}, LabelMapOptions{w, h});
    REQUIRE(result.has_value());
    const Segmentation& seg = *result;

    // Invariant : chaque pixel appartient a zero ou exactement une region --
    // trivialement vrai pour Segmentation::labels (un seul uint32 par pixel),
    // on verifie donc que la region gagnante sur la colonne 2 est bien A
    // (stability/iou plus eleves), et que B garde ses pixels non disputes.
    for (int y = 0; y < h; ++y) {
        CHECK(seg.labels[static_cast<std::size_t>(y) * w + 0] == 1);  // A
        CHECK(seg.labels[static_cast<std::size_t>(y) * w + 1] == 1);  // A
        CHECK(seg.labels[static_cast<std::size_t>(y) * w + 2] == 1);  // A gagne le conflit
        CHECK(seg.labels[static_cast<std::size_t>(y) * w + 3] == 2);  // B, non dispute
    }
    CHECK(seg.find(RegionId{1})->pixel_count == 12);  // 3 colonnes entieres
    CHECK(seg.find(RegionId{2})->pixel_count == 4);   // seule la colonne 3 lui reste
}

TEST_CASE("build_label_map honors an explicit manual priority over automatic scores") {
    constexpr int w = 4;
    constexpr int h = 4;
    LabelMaskInput a;
    a.mask_id = 0;
    a.pixels = columnMask(w, h, 0, 3);
    a.stability_score = 0.9;  // automatiquement gagnant...
    a.predicted_iou = 0.9;

    LabelMaskInput b;
    b.mask_id = 1;
    b.pixels = columnMask(w, h, 2, 4);
    b.stability_score = 0.1;
    b.predicted_iou = 0.1;
    b.manual_priority_set = true;
    b.manual_priority = 0;  // ...mais l'utilisateur force B en priorite

    const auto result = build_label_map({a, b}, LabelMapOptions{w, h});
    REQUIRE(result.has_value());
    // B (manuel) gagne le conflit sur la colonne 2 malgre des scores plus faibles.
    CHECK((*result).labels[2] == 2);
}

TEST_CASE("build_label_map rejects an empty mask list") {
    const auto result = build_label_map({}, LabelMapOptions{4, 4});
    CHECK_FALSE(result.has_value());
}

TEST_CASE("build_label_map rejects a mask whose pixel buffer size mismatches the map") {
    LabelMaskInput a;
    a.pixels.assign(3, 1);  // devrait etre 16 pour une grille 4x4
    const auto result = build_label_map({a}, LabelMapOptions{4, 4});
    CHECK_FALSE(result.has_value());
}
