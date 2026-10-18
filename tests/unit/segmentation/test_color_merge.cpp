// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <vector>

#include "openstitch/segmentation/color_merge.hpp"

using namespace openstitch;
using namespace openstitch::segmentation;

namespace {

using Rgb = std::array<std::uint8_t, 3>;

constexpr Rgb kRed{220, 30, 30};
constexpr Rgb kRedish{210, 40, 35}; // quasi rouge
constexpr Rgb kBlue{30, 30, 220};
constexpr Rgb kGreen{30, 200, 30};

// Bandes verticales de 3 px de large sur 4 px de haut : une région par couleur, ids 1..n.
Segmentation bands(const std::vector<Rgb>& colors) {
    const int bandWidth = 3;
    Segmentation seg;
    seg.height = 4;
    seg.width = bandWidth * static_cast<int>(colors.size());
    seg.labels.resize(static_cast<std::size_t>(seg.width) * static_cast<std::size_t>(seg.height));
    for (int y = 0; y < seg.height; ++y) {
        for (int x = 0; x < seg.width; ++x) {
            seg.labels[static_cast<std::size_t>(y * seg.width + x)] =
                static_cast<std::uint32_t>(x / bandWidth + 1);
        }
    }
    for (std::size_t i = 0; i < colors.size(); ++i) {
        seg.region_slots.push_back(
            Region{RegionId{i + 1}, colors[i], static_cast<std::size_t>(bandWidth * seg.height)});
    }
    return seg;
}

} // namespace

TEST_CASE("color_classes: teintes distinctes triees par aire decroissante") {
    // Deux bandes rouges separees par du bleu : 6 px rouges, 3 px bleus, 3 px verts.
    const Segmentation seg = bands({kRed, kBlue, kRed, kGreen});
    const auto classes = color_classes(seg);
    REQUIRE(classes.size() == 3);
    CHECK(classes[0].rgb == kRed);
    CHECK(classes[0].pixel_count == 24);
    CHECK(classes[0].regions == std::vector<RegionId>{RegionId{1}, RegionId{3}});
    // Egalite d'aire : couleur croissante (bleu < vert en premier canal egal, puis vert < ...).
    CHECK(classes[1].rgb == kBlue);
    CHECK(classes[2].rgb == kGreen);
}

TEST_CASE("fusion manuelle de teintes : recolore, fusionne les voisines, annulable par copie") {
    const Segmentation original = bands({kRed, kRedish, kBlue});
    const std::vector<Rgb> sources{kRedish};
    const auto plan = plan_merge_colors(original, sources, kRed, true);
    REQUIRE(plan.recolors.size() == 1);
    CHECK(plan.recolors[0].first == RegionId{2});
    CHECK(plan.recolors[0].second == kRed);
    REQUIRE(plan.merges.size() == 1);
    CHECK(plan.merges[0] == std::pair{RegionId{1}, RegionId{2}}); // egalite d'aire : plus petit id
    CHECK(plan.colors_before == 3);
    CHECK(plan.colors_after == 2);

    Segmentation seg = original;
    REQUIRE(apply_color_merge(seg, plan).has_value());
    CHECK(seg.region_count() == 2);
    CHECK(seg.find(RegionId{1})->pixel_count == 24);
    CHECK(seg.find(RegionId{2}) == nullptr);
    CHECK(seg.find(RegionId{3})->rgb == kBlue);

    // Sans fusion des voisines : meme recoloration, deux regions rouges distinctes.
    const auto noMerge = plan_merge_colors(original, sources, kRed, false);
    CHECK(noMerge.merges.empty());
    Segmentation seg2 = original;
    REQUIRE(apply_color_merge(seg2, noMerge).has_value());
    CHECK(seg2.region_count() == 3);
    CHECK(seg2.find(RegionId{2})->rgb == kRed);
}

TEST_CASE("fusion de teintes : des regions non voisines ne sont jamais fusionnees") {
    // Rouge, bleu, quasi-rouge : apres recoloration les deux rouges sont separes par le bleu.
    const Segmentation seg = bands({kRed, kBlue, kRedish});
    const std::vector<Rgb> sources{kRedish};
    const auto plan = plan_merge_colors(seg, sources, kRed, true);
    CHECK(plan.recolors.size() == 1);
    CHECK(plan.merges.empty());
}

TEST_CASE("reduction de palette : le plus gros aplat garde sa teinte, le plan est deterministe") {
    // Le rouge occupe deux bandes (6 px), le quasi-rouge une seule : il prend la teinte du rouge.
    const Segmentation seg = bands({kRed, kRedish, kRed, kBlue, kGreen});
    const auto plan = plan_reduce_colors(seg, 3, 0.0, false);
    CHECK(plan.colors_before == 4);
    CHECK(plan.colors_after == 3);
    REQUIRE(plan.recolors.size() == 1);
    CHECK(plan.recolors[0].first == RegionId{2});
    CHECK(plan.recolors[0].second == kRed);

    const auto again = plan_reduce_colors(seg, 3, 0.0, false);
    CHECK(again.recolors == plan.recolors);
    CHECK(again.merges == plan.merges);

    // Sans limite ni seuil : rien a faire.
    CHECK(plan_reduce_colors(seg, 0, 0.0, true).empty());
}

TEST_CASE("reduction de palette par seuil de distance CIEDE2000") {
    const Segmentation seg = bands({kRed, kRedish, kBlue, kGreen});
    // Seuil bas : seule la paire quasi identique fusionne.
    const auto close = plan_reduce_colors(seg, 0, 8.0, true);
    CHECK(close.colors_after == 3);
    CHECK(close.recolors.size() == 1);
    CHECK(close.merges.size() == 1); // rouge et quasi-rouge sont voisines
    // Seuil enorme : tout fusionne en une teinte.
    const auto all = plan_reduce_colors(seg, 0, 1000.0, false);
    CHECK(all.colors_after == 1);
}

TEST_CASE("la couleur exclue (fond ignore) n'est ni recoloree ni fusionnee") {
    const Segmentation seg = bands({kRed, kRedish, kGreen});
    const auto plan = plan_reduce_colors(seg, 1, 0.0, true, std::optional<Rgb>{kGreen});
    for (const auto& [id, rgb] : plan.recolors) {
        (void)rgb;
        CHECK(id != RegionId{3});
    }
    for (const auto& [keep, absorb] : plan.merges) {
        CHECK(keep != RegionId{3});
        CHECK(absorb != RegionId{3});
    }
    CHECK(plan.colors_after == 1); // le vert exclu ne compte pas
}

TEST_CASE("apply_color_merge: region inconnue -> erreur") {
    Segmentation seg = bands({kRed, kBlue});
    ColorMergePlan plan;
    plan.recolors.emplace_back(RegionId{9}, kRed);
    CHECK_FALSE(apply_color_merge(seg, plan).has_value());
}
