// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/segmentation/segmentation.hpp"

using namespace openstitch;
using namespace openstitch::segmentation;

namespace {

void set_px(image::Image& img, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b,
            std::uint8_t a = 255) {
    std::uint8_t* px = img.rgba.data() +
                       (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                        static_cast<std::size_t>(x)) * 4;
    px[0] = r;
    px[1] = g;
    px[2] = b;
    px[3] = a;
}

image::Image blank(int w, int h) {
    image::Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    return img;
}

// Image 8x8 en 4 quadrants : rouge, vert, bleu, jaune.
image::Image quadrants() {
    image::Image img = blank(8, 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (x < 4 && y < 4) set_px(img, x, y, 220, 30, 30);
            else if (x >= 4 && y < 4) set_px(img, x, y, 30, 200, 30);
            else if (x < 4) set_px(img, x, y, 30, 30, 220);
            else set_px(img, x, y, 230, 220, 40);
        }
    }
    return img;
}

}  // namespace

TEST_CASE("quatre quadrants -> quatre regions selectionnables") {
    const auto seg = segment(quadrants(), {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());
    CHECK(seg->region_count() == 4);

    const auto a = region_at(*seg, 1, 1);
    const auto b = region_at(*seg, 6, 1);
    const auto c = region_at(*seg, 1, 6);
    const auto d = region_at(*seg, 6, 6);
    REQUIRE((a && b && c && d));
    CHECK(a != b);
    CHECK(a != c);
    CHECK(a != d);
    CHECK(b != c);

    // La couleur representative du quadrant rouge est bien rougeatre.
    const Region* ra = seg->find(*a);
    REQUIRE(ra != nullptr);
    CHECK(ra->rgb[0] > 150);
    CHECK(ra->rgb[1] < 100);
    CHECK(ra->pixel_count == 16);
}

TEST_CASE("deux regions de meme couleur restent distinctes") {
    // Deux carres rouges separes par une colonne verte.
    image::Image img = blank(7, 3);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 7; ++x) {
            if (x < 3) set_px(img, x, y, 220, 30, 30);
            else if (x == 3) set_px(img, x, y, 30, 200, 30);
            else set_px(img, x, y, 220, 30, 30);
        }
    }
    const auto seg = segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    CHECK(seg->region_count() == 3);
    CHECK(region_at(*seg, 0, 1) != region_at(*seg, 6, 1));
}

TEST_CASE("pixels transparents = fond, sans region") {
    image::Image img = quadrants();
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 4; ++x) {
            set_px(img, x, y, 0, 0, 0, 0);  // moitie gauche transparente
        }
    }
    const auto seg = segment(img, {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());
    CHECK_FALSE(region_at(*seg, 1, 1).has_value());
    CHECK(region_at(*seg, 6, 1).has_value());
}

TEST_CASE("image entierement transparente -> erreur utilisateur") {
    const auto seg = segment(blank(4, 4), {.max_colors = 4, .min_region_px = 1});
    REQUIRE_FALSE(seg.has_value());
    CHECK(seg.error().category == ErrorCategory::UserInput);
}

TEST_CASE("nettoyage des petites regions") {
    // Fond rouge avec un seul pixel bleu : absorbe par la voisine.
    image::Image img = blank(8, 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            set_px(img, x, y, 220, 30, 30);
        }
    }
    set_px(img, 4, 4, 30, 30, 220);
    const auto seg = segment(img, {.max_colors = 2, .min_region_px = 4});
    REQUIRE(seg.has_value());
    CHECK(seg->region_count() == 1);
    // Le pixel bleu appartient desormais a la region rouge.
    CHECK(region_at(*seg, 4, 4) == region_at(*seg, 0, 0));
}

TEST_CASE("fusion : ids stables, comptes corrects, indices renvoyes") {
    auto seg = segment(quadrants(), {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto a = *region_at(*seg, 1, 1);
    const auto b = *region_at(*seg, 6, 1);
    const auto c = *region_at(*seg, 1, 6);

    const auto changed = merge_regions(*seg, a, b);
    REQUIRE(changed.has_value());
    CHECK(changed->size() == 16);
    CHECK(seg->region_count() == 3);
    CHECK(seg->find(b) == nullptr);
    CHECK(seg->find(a)->pixel_count == 32);
    CHECK(*region_at(*seg, 6, 1) == a);
    CHECK(seg->find(c) != nullptr);  // les autres ids restent valides

    CHECK_FALSE(merge_regions(*seg, a, a).has_value());
    CHECK_FALSE(merge_regions(*seg, a, b).has_value());  // b n'existe plus
}

TEST_CASE("suppression : la region disparait (retour au fond, pas de fusion)") {
    auto seg = segment(quadrants(), {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto a = *region_at(*seg, 1, 1);
    const auto voisine = *region_at(*seg, 6, 1);

    const auto result = remove_region(*seg, a);
    REQUIRE(result.has_value());
    CHECK_FALSE(result->first.valid());  // pixels renvoyes au fond, pas a une region
    CHECK(result->second.size() == 16);
    CHECK(seg->region_count() == 3);
    // Le quadrant supprime est desormais du fond (aucune region).
    CHECK_FALSE(region_at(*seg, 1, 1).has_value());
    // Les voisines ne recuperent PAS ses pixels : leur taille est inchangee.
    CHECK(seg->find(voisine)->pixel_count == 16);
}

TEST_CASE("recoloration") {
    auto seg = segment(quadrants(), {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto a = *region_at(*seg, 1, 1);
    const auto old = recolor_region(*seg, a, {1, 2, 3});
    REQUIRE(old.has_value());
    CHECK(seg->find(a)->rgb == std::array<std::uint8_t, 3>{1, 2, 3});
    CHECK((*old)[0] > 150);  // ancienne couleur rougeatre renvoyee
}

TEST_CASE("determinisme : deux segmentations identiques") {
    const auto a = segment(quadrants(), {.max_colors = 4, .min_region_px = 1});
    const auto b = segment(quadrants(), {.max_colors = 4, .min_region_px = 1});
    REQUIRE((a.has_value() && b.has_value()));
    CHECK(a->labels == b->labels);
}

TEST_CASE("lissage desactive par defaut : comportement inchange") {
    const auto seg = segment(quadrants(), {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());
    CHECK(seg->region_count() == 4);
}

TEST_CASE("lissage : bruit poivre-et-sel absorbe par la classe majoritaire locale") {
    image::Image img = blank(9, 9);
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 9; ++x) {
            set_px(img, x, y, 220, 30, 30);  // rouge partout
        }
    }
    // Quelques pixels de bruit bleu, isoles (aucun voisin bleu adjacent).
    set_px(img, 1, 1, 30, 30, 220);
    set_px(img, 4, 2, 30, 30, 220);
    set_px(img, 7, 4, 30, 30, 220);
    set_px(img, 2, 6, 30, 30, 220);
    set_px(img, 6, 7, 30, 30, 220);

    const auto noisy = segment(img, {.max_colors = 2, .min_region_px = 1, .smoothing_radius_px = 0});
    REQUIRE(noisy.has_value());
    CHECK(noisy->region_count() > 1);  // le bruit forme des regions bleues isolees

    const auto smoothed = segment(img, {.max_colors = 2, .min_region_px = 1, .smoothing_radius_px = 2});
    REQUIRE(smoothed.has_value());
    CHECK(smoothed->region_count() == 1);  // absorbe par le vote majoritaire local (rouge)
}

TEST_CASE("lissage : determinisme preserve") {
    image::Image img = quadrants();
    const auto a = segment(img, {.max_colors = 4, .min_region_px = 1, .smoothing_radius_px = 2});
    const auto b = segment(img, {.max_colors = 4, .min_region_px = 1, .smoothing_radius_px = 2});
    REQUIRE((a.has_value() && b.has_value()));
    CHECK(a->labels == b->labels);
}

TEST_CASE("lissage : les pixels transparents restent hors de toute region") {
    image::Image img = quadrants();
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 4; ++x) {
            set_px(img, x, y, 0, 0, 0, 0);
        }
    }
    const auto seg = segment(img, {.max_colors = 4, .min_region_px = 1, .smoothing_radius_px = 2});
    REQUIRE(seg.has_value());
    CHECK_FALSE(region_at(*seg, 1, 1).has_value());
    CHECK(region_at(*seg, 6, 1).has_value());
}

TEST_CASE("rendu de la carte : fond transparent, selection eclaircie") {
    auto seg = segment(quadrants(), {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto a = *region_at(*seg, 1, 1);

    const auto plain = render_map(*seg);
    const auto highlighted = render_map(*seg, a);
    CHECK(plain.width == 8);
    // Pixel de la region selectionnee eclairci, les autres inchanges.
    const std::size_t insideIdx = (1 * 8 + 1) * 4;
    const std::size_t outsideIdx = (1 * 8 + 6) * 4;
    CHECK(highlighted.rgba[insideIdx] > plain.rgba[insideIdx]);
    CHECK(highlighted.rgba[outsideIdx] == plain.rgba[outsideIdx]);
}
