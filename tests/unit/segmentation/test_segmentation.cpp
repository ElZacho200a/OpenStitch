// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "openstitch/segmentation/segmentation.hpp"

using namespace openstitch;
using namespace openstitch::segmentation;

namespace {

void set_px(image::Image& img, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b,
            std::uint8_t a = 255) {
    std::uint8_t* px =
        img.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                           static_cast<std::size_t>(x)) *
                              4;
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
            if (x < 4 && y < 4)
                set_px(img, x, y, 220, 30, 30);
            else if (x >= 4 && y < 4)
                set_px(img, x, y, 30, 200, 30);
            else if (x < 4)
                set_px(img, x, y, 30, 30, 220);
            else
                set_px(img, x, y, 230, 220, 40);
        }
    }
    return img;
}

} // namespace

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
            if (x < 3)
                set_px(img, x, y, 220, 30, 30);
            else if (x == 3)
                set_px(img, x, y, 30, 200, 30);
            else
                set_px(img, x, y, 220, 30, 30);
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
            set_px(img, x, y, 0, 0, 0, 0); // moitie gauche transparente
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
    CHECK(seg->find(c) != nullptr); // les autres ids restent valides

    CHECK_FALSE(merge_regions(*seg, a, a).has_value());
    CHECK_FALSE(merge_regions(*seg, a, b).has_value()); // b n'existe plus
}

TEST_CASE("suppression : la region disparait (retour au fond, pas de fusion)") {
    auto seg = segment(quadrants(), {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto a = *region_at(*seg, 1, 1);
    const auto voisine = *region_at(*seg, 6, 1);

    const auto result = remove_region(*seg, a);
    REQUIRE(result.has_value());
    CHECK_FALSE(result->first.valid()); // pixels renvoyes au fond, pas a une region
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
    CHECK((*old)[0] > 150); // ancienne couleur rougeatre renvoyee
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
            set_px(img, x, y, 220, 30, 30); // rouge partout
        }
    }
    // Quelques pixels de bruit bleu, isoles (aucun voisin bleu adjacent).
    set_px(img, 1, 1, 30, 30, 220);
    set_px(img, 4, 2, 30, 30, 220);
    set_px(img, 7, 4, 30, 30, 220);
    set_px(img, 2, 6, 30, 30, 220);
    set_px(img, 6, 7, 30, 30, 220);

    const auto noisy =
        segment(img, {.max_colors = 2, .min_region_px = 1, .smoothing_radius_px = 0});
    REQUIRE(noisy.has_value());
    CHECK(noisy->region_count() > 1); // le bruit forme des regions bleues isolees

    const auto smoothed =
        segment(img, {.max_colors = 2, .min_region_px = 1, .smoothing_radius_px = 2});
    REQUIRE(smoothed.has_value());
    CHECK(smoothed->region_count() == 1); // absorbe par le vote majoritaire local (rouge)
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

// --- Lot A (audit marine plein cadre) : fond présumé ------------------------

namespace {

// Image opaque w x h remplie de `bg`, avec un rectangle `fg` [x0,x1)x[y0,y1).
image::Image framed(int w, int h, std::array<std::uint8_t, 3> bg, std::array<std::uint8_t, 3> fg,
                    int x0, int y0, int x1, int y1) {
    image::Image img = blank(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            const auto& c = inside ? fg : bg;
            set_px(img, x, y, c[0], c[1], c[2]);
        }
    }
    return img;
}

} // namespace

TEST_CASE("cielab_lightness : blanc 100, noir 0, bleu du ciel sombre") {
    CHECK(cielab_lightness({255, 255, 255}) > 99.9);
    CHECK(cielab_lightness({0, 0, 0}) < 0.1);
    // Bleu du ciel de l'image d'exemple (sample/, L* ~ 43).
    const double sky = cielab_lightness({57, 93, 213});
    CHECK(sky > 40.0);
    CHECK(sky < 47.0);
}

TEST_CASE("fond blanc qui encadre le motif -> ignorer recommande") {
    const auto img = framed(40, 30, {250, 250, 250}, {200, 30, 30}, 10, 8, 30, 22);
    const auto seg = segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto cand = background_candidate(*seg);
    REQUIRE(cand.has_value());
    CHECK(cand->sides_touched == 4);
    CHECK(cand->lightness > 90.0);
    CHECK(cand->area_ratio > 0.7);
    CHECK(cand->area_ratio < 0.8);
    CHECK(cand->recommended);
}

TEST_CASE("image plein cadre dont la plus grande region est coloree -> jamais ignoree") {
    // Ciel bleu sur la moitié haute, mer verte (plus petite) en bas : la plus
    // grande région touche 3 bords mais n'est pas claire -- c'est un vrai
    // élément du motif (défaut de l'audit : le ciel n'était pas brodé).
    const auto img = framed(40, 30, {57, 93, 213}, {6, 101, 60}, 0, 20, 40, 30);
    const auto seg = segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto cand = background_candidate(*seg);
    REQUIRE(cand.has_value());
    CHECK(cand->rgb[2] > cand->rgb[1]); // le bleu est bien la candidate
    CHECK(cand->sides_touched == 3);
    CHECK_FALSE(cand->recommended);
}

TEST_CASE("region blanche centrale qui ne touche pas les bords -> pas recommandee") {
    const auto img = framed(40, 30, {200, 30, 30}, {250, 250, 250}, 2, 2, 38, 28);
    const auto seg = segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto cand = background_candidate(*seg);
    REQUIRE(cand.has_value());
    CHECK(cand->lightness > 90.0);
    CHECK(cand->sides_touched == 0);
    CHECK_FALSE(cand->recommended);
}

TEST_CASE("fond blanc ne touchant que 2 bords -> pas recommande par defaut") {
    // Blanc sur un coin (L inversé), motif rouge ailleurs.
    image::Image img = blank(40, 30);
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 40; ++x) {
            const bool white = x < 32 && y < 24; // 768 px blancs contre 432 rouges
            set_px(img, x, y, white ? 250 : 200, white ? 250 : 30, white ? 250 : 30);
        }
    }
    const auto seg = segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto cand = background_candidate(*seg);
    REQUIRE(cand.has_value());
    CHECK(cand->sides_touched == 2);
    CHECK_FALSE(cand->recommended);
    // Le seuil de bords reste réglable.
    const auto lax = background_candidate(*seg, {.min_lightness = 90.0, .min_sides_touched = 2});
    REQUIRE(lax.has_value());
    CHECK(lax->recommended);
}

TEST_CASE("region_adjacency : longueurs de frontiere des quatre quadrants") {
    const auto seg = segment(quadrants(), {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto borders = region_adjacency(*seg);
    // 4 quadrants 4x4 : chaque paire côte à côte partage 4 arêtes de pixels,
    // les paires diagonales ne se touchent pas (4-connexité).
    REQUIRE(borders.size() == 4);
    for (const auto& b : borders) {
        CHECK(b.a.value < b.b.value);
        CHECK(b.length == 4);
    }
    // Déterministe et trié.
    CHECK(borders == region_adjacency(*seg));
    CHECK(std::is_sorted(borders.begin(), borders.end(), [](const auto& l, const auto& r) {
        return std::pair{l.a.value, l.b.value} < std::pair{r.a.value, r.b.value};
    }));
}

TEST_CASE("region_adjacency : le fond transparent n'est jamais une region voisine") {
    image::Image img = blank(6, 6); // tout transparent
    for (int y = 1; y < 5; ++y) {
        for (int x = 1; x < 5; ++x) {
            set_px(img, x, y, 200, 30, 30);
        }
    }
    const auto seg = segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    CHECK(region_adjacency(*seg).empty());
}

// --- Lot D (audit marine) : fragments -----------------------------------------

namespace {

// Région vivante contenant le pixel (x, y), ou nullptr.
const Region* region_under(const Segmentation& seg, int x, int y) {
    const auto id = region_at(seg, x, y);
    return id ? seg.find(*id) : nullptr;
}

// Rouge [0,15) | bleu [15,30) sur 30x10, avec des pixels verts optionnels.
image::Image halves_with(const std::vector<std::pair<int, int>>& green) {
    image::Image img = blank(30, 10);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 30; ++x) {
            if (x < 15) {
                set_px(img, x, y, 220, 30, 30);
            } else {
                set_px(img, x, y, 30, 30, 220);
            }
        }
    }
    for (const auto& [x, y] : green) {
        set_px(img, x, y, 30, 200, 30);
    }
    return img;
}

} // namespace

TEST_CASE("merge_small_regions : un fragment rejoint la voisine de plus longue frontiere") {
    // Bloc vert 2x3 (x 13..14, y 2..4) : 7 arêtes avec le rouge, 3 avec le bleu.
    const auto seg0 = segment(halves_with({{13, 2}, {14, 2}, {13, 3}, {14, 3}, {13, 4}, {14, 4}}),
                              {.max_colors = 3, .min_region_px = 1});
    REQUIRE(seg0.has_value());
    REQUIRE(seg0->region_count() == 3);
    Segmentation seg = *seg0;
    const auto* redBefore = region_under(seg, 0, 0);
    REQUIRE(redBefore != nullptr);
    const RegionId red = redBefore->id;
    const std::size_t redCount = redBefore->pixel_count;

    CHECK(merge_small_regions(seg, 10) == 1);
    CHECK(seg.region_count() == 2);
    CHECK(region_under(seg, 13, 3)->id == red);
    CHECK(seg.find(red)->pixel_count == redCount + 6);

    // Déterministe.
    Segmentation again = *seg0;
    CHECK(merge_small_regions(again, 10) == 1);
    CHECK(again.labels == seg.labels);
}

TEST_CASE("merge_small_regions : jamais dans une couleur exclue (fond ignore)") {
    // Vert entouré de rouge seul : si le rouge est le fond ignoré, le
    // fragment n'a aucune voisine admissible et reste tel quel (il sera
    // cousu en contour, isolé au milieu du fond).
    const auto seg0 = segment(halves_with({{5, 4}, {6, 4}, {5, 5}, {6, 5}}),
                              {.max_colors = 3, .min_region_px = 1});
    REQUIRE(seg0.has_value());
    Segmentation seg = *seg0;
    const auto redRgb = region_under(seg, 0, 0)->rgb;
    CHECK(merge_small_regions(seg, 10, redRgb) == 0);
    CHECK(seg.region_count() == 3);
}

TEST_CASE("remove_thin_parts : une lamelle de 1 px rejoint sa voisine, le corps reste") {
    // Carré rouge 20x20 (x 0..19) + lamelle rouge de 1 px (y = 10) qui
    // s'avance dans le bleu (x 20..29).
    image::Image img = blank(40, 20);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 40; ++x) {
            const bool red = x < 20 || (y == 10 && x < 30);
            set_px(img, x, y, red ? 220 : 30, 30, red ? 30 : 220);
        }
    }
    const auto seg0 = segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg0.has_value());
    Segmentation seg = *seg0;
    const RegionId red = region_under(seg, 0, 0)->id;
    const RegionId blue = region_under(seg, 39, 0)->id;
    CHECK(region_under(seg, 25, 10)->id == red);

    // 10 px de lamelle ; le noyau elliptique (croix en 3x3) arrondit aussi
    // les deux coins convexes du carré côté bleu -- rien d'autre.
    const std::size_t moved = remove_thin_parts(seg, 3);
    CHECK(moved >= 10);
    CHECK(moved <= 12);
    CHECK(region_under(seg, 25, 10)->id == blue);
    CHECK(region_under(seg, 21, 10)->id == blue);
    CHECK(region_under(seg, 10, 10)->id == red); // le corps du carré reste
    CHECK(seg.find(red)->pixel_count + seg.find(blue)->pixel_count == 800);
    CHECK(seg.find(red)->pixel_count >= 398);

    // Largeur < 2 px : sans effet (aucune ouverture possible).
    Segmentation same = *seg0;
    CHECK(remove_thin_parts(same, 1) == 0);
    CHECK(same.labels == seg0->labels);
}

TEST_CASE("remove_thin_parts : une lamelle isolee dans le vide reste en place") {
    image::Image img = blank(20, 10); // transparent
    for (int x = 2; x < 18; ++x) {
        set_px(img, x, 5, 220, 30, 30);
    }
    const auto seg0 = segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg0.has_value());
    Segmentation seg = *seg0;
    CHECK(remove_thin_parts(seg, 3) == 0);
    CHECK(seg.region_count() == 1);
}

namespace {

// Nettoyage des petites régions tel qu'implémenté AVANT l'audit de
// performance 2026-09 (balayage complet de l'image par région), recopié
// comme référence d'équivalence.
void reference_small_region_cleanup(Segmentation& seg, int min_region_px) {
    const int w = seg.width;
    const int h = seg.height;
    const auto majority = [&](std::uint32_t label) -> std::optional<std::uint32_t> {
        std::map<std::uint32_t, std::size_t> counts;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (seg.labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                               static_cast<std::size_t>(x)] != label) {
                    continue;
                }
                const auto visit = [&](int nx, int ny) {
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                        return;
                    }
                    const std::uint32_t other =
                        seg.labels[static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) +
                                   static_cast<std::size_t>(nx)];
                    if (other != label && other != 0) {
                        ++counts[other];
                    }
                };
                visit(x - 1, y);
                visit(x + 1, y);
                visit(x, y - 1);
                visit(x, y + 1);
            }
        }
        if (counts.empty()) {
            return std::nullopt;
        }
        return std::max_element(counts.begin(), counts.end(),
                                [](const auto& a, const auto& b) { return a.second < b.second; })
            ->first;
    };
    std::vector<std::size_t> order;
    for (std::size_t s = 0; s < seg.region_slots.size(); ++s) {
        if (seg.region_slots[s] &&
            seg.region_slots[s]->pixel_count < static_cast<std::size_t>(min_region_px)) {
            order.push_back(s);
        }
    }
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return seg.region_slots[a]->pixel_count < seg.region_slots[b]->pixel_count;
    });
    for (const std::size_t s : order) {
        if (!seg.region_slots[s]) {
            continue;
        }
        const auto label = static_cast<std::uint32_t>(s + 1);
        const std::uint32_t target = majority(label).value_or(0);
        std::size_t changed = 0;
        for (auto& l : seg.labels) {
            if (l == label) {
                l = target;
                ++changed;
            }
        }
        if (target != 0) {
            seg.region_slots[target - 1]->pixel_count += changed;
        }
        seg.region_slots[s].reset();
    }
}

// Image bruitée déterministe : blocs de couleur + bruit pixel (beaucoup de
// petites régions en cascade) + quelques pixels transparents.
image::Image noisy_image(int w, int h, unsigned seed) {
    image::Image img = blank(w, h);
    std::uint32_t state = seed;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int block = ((x / 7) + (y / 5)) % 4;
            std::uint8_t r = static_cast<std::uint8_t>(40 + 60 * block);
            std::uint8_t g = static_cast<std::uint8_t>(200 - 40 * block);
            std::uint8_t b = static_cast<std::uint8_t>(90 + 30 * ((x / 11) % 3));
            if (next() % 5 == 0) {
                r = static_cast<std::uint8_t>(next() % 256);
                g = static_cast<std::uint8_t>(next() % 256);
                b = static_cast<std::uint8_t>(next() % 256);
            }
            const std::uint8_t a = (next() % 97 == 0) ? 0 : 255;
            set_px(img, x, y, r, g, b, a);
        }
    }
    return img;
}

} // namespace

TEST_CASE("nettoyage des petites regions : identique a l'implementation de reference",
          "[segmentation][perf]") {
    for (const auto [w, h, seed] :
         {std::tuple{64, 48, 1u}, std::tuple{120, 90, 7u}, std::tuple{200, 60, 42u}}) {
        const image::Image img = noisy_image(w, h, seed);
        for (const int minPx : {4, 16, 50}) {
            for (const int smoothing : {0, 1}) {
                INFO("image " << w << "x" << h << " graine " << seed << " min " << minPx
                              << " lissage " << smoothing);
                auto raw = segment(img, {.max_colors = 8,
                                         .min_region_px = 1, // aucun nettoyage
                                         .smoothing_radius_px = smoothing});
                REQUIRE(raw.has_value());
                reference_small_region_cleanup(*raw, minPx);
                const auto fast = segment(
                    img,
                    {.max_colors = 8, .min_region_px = minPx, .smoothing_radius_px = smoothing});
                REQUIRE(fast.has_value());
                CHECK(fast->labels == raw->labels);
                REQUIRE(fast->region_slots.size() == raw->region_slots.size());
                for (std::size_t s = 0; s < fast->region_slots.size(); ++s) {
                    REQUIRE(fast->region_slots[s].has_value() == raw->region_slots[s].has_value());
                    if (fast->region_slots[s]) {
                        CHECK(fast->region_slots[s]->pixel_count ==
                              raw->region_slots[s]->pixel_count);
                        CHECK(fast->region_slots[s]->rgb == raw->region_slots[s]->rgb);
                    }
                }
            }
        }
    }
}

TEST_CASE("merge_small_regions : taille effective = pixels - poids x frontiere") {
    // Bande verte 2x5 px (10 px) dans le rouge : 14 arêtes de frontière.
    std::vector<std::pair<int, int>> green;
    for (int y = 2; y < 7; ++y) {
        green.emplace_back(5, y);
        green.emplace_back(6, y);
    }
    const auto seg0 = segment(halves_with(green), {.max_colors = 3, .min_region_px = 1});
    REQUIRE(seg0.has_value());
    Segmentation plain = *seg0;
    CHECK(merge_small_regions(plain, 8) == 0); // 10 px >= 8
    Segmentation weighted = *seg0;
    // 10 - 7 + 1 = 4 px effectifs (le polygone vectorisé fait 1 x 4 px) < 8.
    CHECK(merge_small_regions(weighted, 8, std::nullopt, 0.5) == 1);
}

TEST_CASE("aides de selection : regions, couleur, voisines, cadre") {
    const auto seg = segment(quadrants(), {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto red = *region_at(*seg, 1, 1);
    const auto green = *region_at(*seg, 6, 1);
    const auto blue = *region_at(*seg, 1, 6);
    const auto yellow = *region_at(*seg, 6, 6);

    CHECK(all_regions(*seg).size() == 4);
    CHECK(regions_with_color(*seg, seg->find(red)->rgb) == std::vector<RegionId>{red});
    CHECK(regions_with_color(*seg, {1, 2, 3}).empty());

    // Chaque quadrant touche deux voisins (4-connexite), 4 aretes communes chacun.
    const auto n = neighbors_of(*seg, red);
    CHECK(n.size() == 2);
    CHECK(std::find(n.begin(), n.end(), green) != n.end());
    CHECK(std::find(n.begin(), n.end(), blue) != n.end());
    CHECK(std::find(n.begin(), n.end(), yellow) == n.end()); // en diagonale seulement

    // Cadre : un seul quadrant, deux quadrants, tout ; coordonnees inversees ou hors image.
    CHECK(regions_in_rect(*seg, 0, 0, 2, 2) == std::vector<RegionId>{red});
    CHECK(regions_in_rect(*seg, 2, 2, 5, 2).size() == 2);
    CHECK(regions_in_rect(*seg, 7, 7, 0, 0).size() == 4);
    CHECK(regions_in_rect(*seg, -50, -50, 100, 100).size() == 4);
    CHECK(regions_in_rect(*seg, 20, 20, 30, 30).empty());
}

TEST_CASE("couleur moyenne d'une region dans l'image d'origine") {
    const image::Image original = quadrants();
    auto seg = segment(original, {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto red = *region_at(*seg, 1, 1);
    const auto mean = region_mean_color(*seg, original, red);
    REQUIRE(mean.has_value());
    CHECK((*mean == std::array<std::uint8_t, 3>{220, 30, 30}));

    // Recolorer puis revenir a la couleur d'origine par la couleur moyenne.
    REQUIRE(recolor_region(*seg, red, {0, 0, 0}).has_value());
    CHECK((seg->find(red)->rgb == std::array<std::uint8_t, 3>{0, 0, 0}));
    CHECK(recolor_region(*seg, red, *mean).has_value());
    CHECK((seg->find(red)->rgb == *mean));

    image::Image wrongSize = blank(3, 3);
    CHECK_FALSE(region_mean_color(*seg, wrongSize, red).has_value());
    CHECK_FALSE(region_mean_color(*seg, original, RegionId{99}).has_value());
}

TEST_CASE("carte avec plusieurs regions eclaircies") {
    const auto seg = segment(quadrants(), {.max_colors = 4, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto red = *region_at(*seg, 1, 1);
    const auto green = *region_at(*seg, 6, 1);
    const auto plain = render_map(*seg, std::nullopt);
    const auto both = render_map_multi(*seg, {red, green});
    const auto onlyRed = render_map_multi(*seg, {red});
    CHECK(onlyRed.rgba == render_map(*seg, red).rgba); // meme eclaircissement qu'une seule
    const auto px = [&](const image::Image& m, int x, int y) {
        return m.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) +
                       static_cast<std::size_t>(x)) *
                          4 +
                      1];
    };
    CHECK(px(both, 1, 1) > px(plain, 1, 1));  // rouge eclairci
    CHECK(px(both, 6, 1) > px(plain, 6, 1));  // vert eclairci
    CHECK(px(both, 1, 6) == px(plain, 1, 6)); // bleu inchange
    CHECK(px(both, 6, 6) == px(plain, 6, 6)); // jaune inchange
}
