// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/thread_palette/color_distance.hpp"
#include "openstitch/thread_palette/thread_library.hpp"
#include "openstitch/thread_palette/thread_stock.hpp"

namespace openstitch::thread_palette {

// Pipette (HP-THR-012), partie cœur : échantillonner une couleur sur des données brutes et
// la rapprocher d'un fil. Aucune dépendance à libs/image : l'appelant fournit un tableau
// RGB entrelacé (3 octets par pixel, lignes contiguës, origine en haut à gauche).
struct RgbImageView {
    std::span<const std::uint8_t> data;
    int width = 0;
    int height = 0;
};

// Couleur au pixel (x, y), moyennée sur le carré de côté 2*radius+1 centré dessus, rogné
// aux bords de l'image (radius 0 = le pixel seul). Moyenne arithmétique des valeurs sRGB
// en entiers, arrondie au plus proche (déterministe, sans flottant). Erreur UserInput si
// la vue est incohérente avec ses dimensions, ou si (x, y) est hors image ou radius < 0.
[[nodiscard]] Result<std::array<std::uint8_t, 3>> sample_color(const RgbImageView& image, int x,
                                                               int y, int radius = 0);

// Couleur moyenne du rectangle [x, x+w) x [y, y+h), rogné à l'image. Erreur UserInput si
// le rectangle rogné est vide.
[[nodiscard]] Result<std::array<std::uint8_t, 3>> sample_region(const RgbImageView& image, int x,
                                                                int y, int w, int h);

// Pipette complète : échantillonne (x, y) puis propose les `top_n` fils les plus proches.
// `stock` non nul = recherche restreinte à « Mes fils » (chart_id ignoré) ; sinon recherche
// dans `chart_id` ("" = toute la bibliothèque).
struct EyedropperResult {
    std::array<std::uint8_t, 3> sampled{};
    std::vector<ThreadMatch> matches;
};

[[nodiscard]] Result<EyedropperResult> pick_thread(const RgbImageView& image, int x, int y,
                                                   int radius, const ThreadLibrary& library,
                                                   std::string_view chart_id,
                                                   const ThreadStock* stock, std::size_t top_n);

} // namespace openstitch::thread_palette
