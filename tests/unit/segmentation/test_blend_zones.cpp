// SPDX-License-Identifier: Apache-2.0
// Zones de fondu (2026-11-17) : un dégradé lent reste une zone, un bord net sépare.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>

#include "openstitch/segmentation/blend_zones.hpp"

using namespace openstitch;

namespace {

// 200 × 100 px à 0,5 mm/px. Moitié gauche : dégradé lent de ciel, gris bleu -> bleu pâle de haut
// en bas (≈ 0,3 ΔE par pixel, gradient Sobel ≈ 0,5 < seuil 2,5). Moitié droite : aplat orange
// (bord net au milieu).
image::Image gradient_and_flat() {
    image::Image img;
    img.width = 200;
    img.height = 100;
    img.rgba.resize(static_cast<std::size_t>(img.width * img.height) * 4);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            auto* p = &img.rgba[static_cast<std::size_t>(y * img.width + x) * 4];
            if (x < 100) {
                p[0] = static_cast<std::uint8_t>(120 + y * 80 / 99);
                p[1] = static_cast<std::uint8_t>(140 + y * 60 / 99);
                p[2] = static_cast<std::uint8_t>(200 + y * 30 / 99);
            } else {
                p[0] = 230;
                p[1] = 120;
                p[2] = 30;
            }
            p[3] = 255;
        }
    }
    return img;
}

std::set<std::uint32_t> labels_in(const segmentation::Segmentation& seg, int x0, int x1) {
    std::set<std::uint32_t> out;
    for (int y = 0; y < seg.height; ++y) {
        for (int x = x0; x < x1; ++x) {
            out.insert(seg.labels[static_cast<std::size_t>(y * seg.width + x)]);
        }
    }
    return out;
}

} // namespace

TEST_CASE("blend zones: a slow gradient stays one zone, a sharp edge splits",
          "[segmentation][blend_zones]") {
    const auto img = gradient_and_flat();
    auto seg = segmentation::segment_blend_zones(img, Millimeters{0.5});
    REQUIRE(seg);
    CHECK(seg->width == 200);
    CHECK(seg->height == 100);
    CHECK(seg->region_count() == 2);
    // Loin du bord net (le flou de 0,75 mm l'étale sur quelques pixels).
    const auto left = labels_in(*seg, 0, 95);
    const auto right = labels_in(*seg, 105, 200);
    REQUIRE(left.size() == 1);
    REQUIRE(right.size() == 1);
    CHECK(*left.begin() != *right.begin());
    CHECK(*left.begin() != 0u);
    // Couleur moyenne de la zone droite : l'aplat orange.
    const auto* r = seg->find(RegionId{*right.begin()});
    REQUIRE(r != nullptr);
    CHECK(r->rgb[0] > 200);
    CHECK(r->rgb[2] < 60);
    // Déterminisme.
    const auto again = segmentation::segment_blend_zones(img, Millimeters{0.5});
    REQUIRE(again);
    CHECK(again->labels == seg->labels);
}

TEST_CASE("blend zones: finer image is reduced to the working step",
          "[segmentation][blend_zones]") {
    // Même motif à 0,25 mm/px (400 × 200 px) : mêmes zones, étiquettes à la taille d'origine.
    const auto coarse = gradient_and_flat();
    image::Image fine;
    fine.width = 400;
    fine.height = 200;
    fine.rgba.resize(static_cast<std::size_t>(fine.width * fine.height) * 4);
    for (int y = 0; y < fine.height; ++y) {
        for (int x = 0; x < fine.width; ++x) {
            for (int c = 0; c < 4; ++c) {
                fine.rgba[static_cast<std::size_t>(y * fine.width + x) * 4 + c] =
                    coarse.rgba[static_cast<std::size_t>((y / 2) * coarse.width + x / 2) * 4 + c];
            }
        }
    }
    auto seg = segmentation::segment_blend_zones(fine, Millimeters{0.25});
    REQUIRE(seg);
    CHECK(seg->width == 400);
    CHECK(seg->labels.size() == 400u * 200u);
    CHECK(seg->region_count() == 2);
}

TEST_CASE("blend zones: refuses an empty image", "[segmentation][blend_zones]") {
    CHECK_FALSE(segmentation::segment_blend_zones(image::Image{}, Millimeters{0.5}));
}
