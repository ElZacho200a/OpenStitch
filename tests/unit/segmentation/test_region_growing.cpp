// SPDX-License-Identifier: Apache-2.0
// Croissance de region a partir d'un clic (HP-AUTO-003).
#include <array>
#include <catch2/catch_test_macros.hpp>

#include "openstitch/segmentation/region_growing.hpp"

using namespace openstitch;
using namespace openstitch::segmentation;

namespace {

using Rgb = std::array<std::uint8_t, 3>;

constexpr Rgb kWhite{255, 255, 255};
constexpr Rgb kRed{220, 30, 30};
constexpr Rgb kRedish{214, 36, 34}; // quasi rouge
constexpr Rgb kBlue{30, 30, 220};

image::Image solid(int w, int h, Rgb rgb) {
    image::Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U);
    for (std::size_t i = 0; i < img.rgba.size() / 4U; ++i) {
        img.rgba[i * 4U] = rgb[0];
        img.rgba[i * 4U + 1U] = rgb[1];
        img.rgba[i * 4U + 2U] = rgb[2];
        img.rgba[i * 4U + 3U] = 255;
    }
    return img;
}

void paint(image::Image& img, int x0, int y0, int x1, int y1, Rgb rgb, std::uint8_t alpha = 255) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            auto* p = &img.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                                 static_cast<std::size_t>(x)) *
                                4U];
            p[0] = rgb[0];
            p[1] = rgb[1];
            p[2] = rgb[2];
            p[3] = alpha;
        }
    }
}

} // namespace

TEST_CASE("grow_region selects the connected flat area under the click", "[region_growing]") {
    // Fond blanc 20x10 avec un carre rouge 6x6 en (2..7, 2..7).
    image::Image img = solid(20, 10, kWhite);
    paint(img, 2, 2, 7, 7, kRed);
    const auto grown = grow_region(img, 4, 4, {.tolerance = 5.0});
    REQUIRE(grown.has_value());
    CHECK(grown->pixel_count == 36);
    CHECK(grown->x0 == 2);
    CHECK(grown->y0 == 2);
    CHECK(grown->x1 == 7);
    CHECK(grown->y1 == 7);
    CHECK(grown->contains(2, 2));
    CHECK_FALSE(grown->contains(1, 2));
    CHECK(grown->mean_rgb == kRed);
    CHECK(grown->seed_rgb == kRed);
}

TEST_CASE("grow_region tolerance widens the selection to close colours only", "[region_growing]") {
    image::Image img = solid(12, 4, kWhite);
    paint(img, 0, 0, 3, 3, kRed);
    paint(img, 4, 0, 7, 3, kRedish);
    paint(img, 8, 0, 11, 3, kBlue);
    const auto strict = grow_region(img, 0, 0, {.tolerance = 0.0});
    REQUIRE(strict.has_value());
    CHECK(strict->pixel_count == 16); // couleur exacte seulement
    const auto loose = grow_region(img, 0, 0, {.tolerance = 15.0});
    REQUIRE(loose.has_value());
    CHECK(loose->pixel_count == 32); // le quasi-rouge est absorbe, pas le bleu
    CHECK_FALSE(loose->contains(8, 0));
    const auto huge = grow_region(img, 0, 0, {.tolerance = 400.0});
    REQUIRE(huge.has_value());
    CHECK(huge->pixel_count == 48);
}

TEST_CASE("grow_region does not cross a different colour barrier", "[region_growing]") {
    // Deux zones rouges separees par une colonne bleue : la croissance reste a gauche.
    image::Image img = solid(9, 3, kRed);
    paint(img, 4, 0, 4, 2, kBlue);
    const auto grown = grow_region(img, 0, 1, {.tolerance = 10.0});
    REQUIRE(grown.has_value());
    CHECK(grown->pixel_count == 12);
    CHECK_FALSE(grown->contains(5, 1));
}

TEST_CASE("grow_region diagonal connectivity is opt-in", "[region_growing]") {
    image::Image img = solid(4, 4, kWhite);
    paint(img, 0, 0, 0, 0, kRed);
    paint(img, 1, 1, 1, 1, kRed);
    const auto four = grow_region(img, 0, 0, {.tolerance = 3.0});
    REQUIRE(four.has_value());
    CHECK(four->pixel_count == 1);
    const auto eight = grow_region(img, 0, 0, {.tolerance = 3.0, .connect_diagonals = true});
    REQUIRE(eight.has_value());
    CHECK(eight->pixel_count == 2);
}

TEST_CASE("grow_region rejects clicks outside the image or on transparency", "[region_growing]") {
    image::Image img = solid(5, 5, kRed);
    paint(img, 0, 0, 1, 1, kRed, 0);
    CHECK_FALSE(grow_region(img, -1, 0).has_value());
    CHECK_FALSE(grow_region(img, 5, 0).has_value());
    CHECK_FALSE(grow_region(img, 0, 5).has_value());
    const auto transparent = grow_region(img, 0, 0);
    REQUIRE_FALSE(transparent.has_value());
    CHECK(transparent.error().category == ErrorCategory::UserInput);
    CHECK_FALSE(grow_region(image::Image{}, 0, 0).has_value());
}

TEST_CASE("grow_region never absorbs transparent pixels", "[region_growing]") {
    image::Image img = solid(6, 1, kRed);
    paint(img, 3, 0, 3, 0, kRed, 10); // meme couleur mais transparent : coupe la region
    const auto grown = grow_region(img, 0, 0, {.tolerance = 5.0});
    REQUIRE(grown.has_value());
    CHECK(grown->pixel_count == 3);
}

TEST_CASE("grow_region fills small holes but keeps large ones and the outside",
          "[region_growing]") {
    // Aplat rouge 11x11 avec un trou d'1 px en (3,3) et un trou de 4x4 en (6..9, 6..9).
    image::Image img = solid(13, 13, kWhite);
    paint(img, 1, 1, 11, 11, kRed);
    paint(img, 3, 3, 3, 3, kBlue);
    paint(img, 6, 6, 9, 9, kBlue);
    const auto raw = grow_region(img, 1, 1, {.tolerance = 5.0});
    REQUIRE(raw.has_value());
    CHECK(raw->pixel_count == 121 - 1 - 16);
    const auto filled = grow_region(img, 1, 1, {.tolerance = 5.0, .fill_holes_up_to_px = 4});
    REQUIRE(filled.has_value());
    CHECK(filled->pixel_count == 121 - 16); // le trou d'1 px est comble, celui de 16 px reste
    CHECK(filled->contains(3, 3));
    CHECK_FALSE(filled->contains(7, 7));
    // Le monde exterieur (touche le bord) n'est jamais comble, meme avec un seuil enorme.
    const auto huge = grow_region(img, 1, 1, {.tolerance = 5.0, .fill_holes_up_to_px = 100000});
    REQUIRE(huge.has_value());
    CHECK(huge->pixel_count == 121);
    CHECK_FALSE(huge->contains(0, 0));
}

TEST_CASE("grow_region is deterministic", "[region_growing]") {
    image::Image img = solid(40, 30, kWhite);
    paint(img, 5, 5, 30, 20, kRed);
    paint(img, 10, 10, 12, 12, kRedish);
    const auto a = grow_region(img, 6, 6, {.tolerance = 12.0});
    const auto b = grow_region(img, 6, 6, {.tolerance = 12.0});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->mask == b->mask);
    CHECK(a->mean_rgb == b->mean_rgb);
    CHECK(a->pixel_count == b->pixel_count);
}

TEST_CASE("segmentation_from_grown builds a single-region segmentation", "[region_growing]") {
    image::Image img = solid(10, 10, kWhite);
    paint(img, 2, 3, 5, 6, kBlue);
    const auto grown = grow_region(img, 3, 4, {.tolerance = 5.0});
    REQUIRE(grown.has_value());
    const Segmentation seg = segmentation_from_grown(*grown);
    CHECK(seg.width == 10);
    CHECK(seg.height == 10);
    CHECK(seg.region_count() == 1);
    const Region* region = seg.find(RegionId{1});
    REQUIRE(region != nullptr);
    CHECK(region->pixel_count == 16);
    CHECK(region->rgb == kBlue);
    CHECK(region_at(seg, 3, 4) == std::optional<RegionId>{RegionId{1}});
    CHECK_FALSE(region_at(seg, 0, 0).has_value());
    // Masque vide -> segmentation sans region.
    GrownRegion none;
    none.width = 4;
    none.height = 4;
    none.mask.assign(16, 0);
    CHECK(segmentation_from_grown(none).region_count() == 0);
}

TEST_CASE("render_grown_preview tints only the selected pixels", "[region_growing]") {
    image::Image img = solid(4, 4, kWhite);
    paint(img, 1, 1, 2, 2, kRed);
    const auto grown = grow_region(img, 1, 1, {.tolerance = 3.0});
    REQUIRE(grown.has_value());
    const image::Image preview = render_grown_preview(*grown, {10, 20, 30}, 100);
    REQUIRE(preview.width == 4);
    const auto px = [&preview](int x, int y) {
        return &preview.rgba[(static_cast<std::size_t>(y) * 4U + static_cast<std::size_t>(x)) * 4U];
    };
    CHECK(px(1, 1)[0] == 10);
    CHECK(px(1, 1)[3] == 100);
    CHECK(px(0, 0)[3] == 0);
}
