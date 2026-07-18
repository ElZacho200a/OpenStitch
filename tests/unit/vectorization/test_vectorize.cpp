// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/vectorization/vectorize.hpp"

using namespace openstitch;
using namespace openstitch::vectorization;

namespace {

void set_px(image::Image& img, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::uint8_t* px = img.rgba.data() +
                       (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                        static_cast<std::size_t>(x)) * 4;
    px[0] = r;
    px[1] = g;
    px[2] = b;
    px[3] = 255;
}

// Image 20x20 : anneau rouge (carre exterieur 16x16, trou 6x6) sur fond blanc.
image::Image ring_image() {
    image::Image img;
    img.width = 20;
    img.height = 20;
    img.rgba.assign(20 * 20 * 4, 0);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            const bool inOuter = (x >= 2 && x < 18 && y >= 2 && y < 18);
            const bool inHole = (x >= 7 && x < 13 && y >= 7 && y < 13);
            if (inOuter && !inHole) {
                set_px(img, x, y, 220, 30, 30);
            } else {
                set_px(img, x, y, 250, 250, 250);
            }
        }
    }
    return img;
}

}  // namespace

TEST_CASE("anneau -> un PathSet avec exterieur et un trou") {
    const auto seg = segmentation::segment(ring_image(), {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());

    // La region rouge est celle du pixel (2,2).
    const auto red = segmentation::region_at(*seg, 2, 2);
    REQUIRE(red.has_value());

    const VectorizeOptions options{.mm_per_px = Millimeters{1.0},
                                   .simplify_tolerance = Micrometers{200}};
    const auto sets = vectorize_region(*seg, *red, options);
    REQUIRE(sets.has_value());
    REQUIRE(sets->size() == 1);
    CHECK((*sets)[0].holes.size() == 1);

    // Aire exterieure ~ 16x16 mm = 256 mm² (contour par centres de pixels :
    // ~15x15 = 225 mm²). Trou nominal 6x6 = 36 mm² : le contour d'un trou
    // (RETR_CCOMP) longe les pixels adjacents, soit ~7x7 = 49 mm².
    const double outerArea = std::abs(geometry::signed_area_um2((*sets)[0].outer)) / 1e6;
    const double holeArea = std::abs(geometry::signed_area_um2((*sets)[0].holes[0])) / 1e6;
    CHECK(outerArea > 180.0);
    CHECK(outerArea < 260.0);
    CHECK(holeArea > 20.0);
    CHECK(holeArea < 55.0);
    CHECK(holeArea < outerArea);

    // La simplification reduit le rectangle a une poignee de noeuds.
    CHECK((*sets)[0].outer.nodes.size() <= 8);
}

TEST_CASE("coordonnees physiques : centre de l'image = origine, Y vers le haut") {
    const auto seg = segmentation::segment(ring_image(), {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto red = segmentation::region_at(*seg, 2, 2);
    REQUIRE(red.has_value());

    const auto sets = vectorize_region(
        *seg, *red, {.mm_per_px = Millimeters{1.0}, .simplify_tolerance = Micrometers{200}});
    REQUIRE(sets.has_value());

    // L'anneau est centre : ses bornes doivent etre symetriques autour de 0,
    // et contenues dans la moitie de l'image (10 mm).
    std::int32_t minX = INT32_MAX, maxX = INT32_MIN, minY = INT32_MAX, maxY = INT32_MIN;
    for (const auto& n : (*sets)[0].outer.nodes) {
        minX = std::min(minX, n.pos.x.value);
        maxX = std::max(maxX, n.pos.x.value);
        minY = std::min(minY, n.pos.y.value);
        maxY = std::max(maxY, n.pos.y.value);
    }
    CHECK(minX == -maxX);
    CHECK(minY == -maxY);
    CHECK(maxX < 10'000);
    CHECK(maxY < 10'000);
}

TEST_CASE("region inexistante -> erreur interne propre") {
    const auto seg = segmentation::segment(ring_image(), {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    CHECK_FALSE(vectorize_region(*seg, RegionId{9999}, {}).has_value());
}

TEST_CASE("deux morceaux de meme region... deviennent deux PathSet") {
    // Deux carres rouges disjoints de la MEME couleur : deux regions
    // distinctes, chacune vectorisee separement produit un PathSet.
    image::Image img;
    img.width = 16;
    img.height = 8;
    img.rgba.assign(16 * 8 * 4, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool left = (x >= 1 && x < 6 && y >= 1 && y < 7);
            const bool right = (x >= 10 && x < 15 && y >= 1 && y < 7);
            set_px(img, x, y, (left || right) ? 220 : 250, (left || right) ? 30 : 250,
                   (left || right) ? 30 : 250);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto a = segmentation::region_at(*seg, 2, 2);
    const auto b = segmentation::region_at(*seg, 12, 2);
    REQUIRE((a && b));
    CHECK(*a != *b);  // deux regions distinctes malgre la meme couleur (§4.3)

    const auto setsA = vectorize_region(*seg, *a, {.mm_per_px = Millimeters{1.0}});
    REQUIRE(setsA.has_value());
    CHECK(setsA->size() == 1);
}
