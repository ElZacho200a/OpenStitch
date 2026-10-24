// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::segmentation {

// Croissance de région à partir d'un clic (HP-AUTO-003, « baguette magique » / clic-pour-broder).
//
// Contrairement à `segment`, qui quantifie toute l'image, la croissance ne regarde que le voisinage
// du pixel cliqué : elle est locale, donc assez rapide pour un aperçu pendant que l'utilisateur
// règle la tolérance, et elle suit exactement la couleur vue à l'écran (pas de palette réduite).
struct GrowOptions {
    // Écart maximal à la couleur du pixel cliqué, en CIELAB (ΔE76). Comparé à la couleur de la
    // GRAINE et non au voisin : la région ne dérive pas le long d'un dégradé. 0 = couleur exacte.
    double tolerance{12.0};
    // Vrai : 8-connexité (deux pixels en diagonale sont reliés). Faux (défaut) : 4-connexité, comme
    // `segment`.
    bool connect_diagonals{false};
    // Trous (pixels hors tolérance entièrement entourés par la région) d'au plus ce nombre de
    // pixels comblés : pixels isolés d'anti-crénelage ou de bruit JPEG. 0 = aucun comblement.
    std::size_t fill_holes_up_to_px{0};
};

// Résultat de la croissance : un masque binaire de la taille de l'image.
struct GrownRegion {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> mask;      // width*height, 1 = pixel de la région
    std::size_t pixel_count{0};          // nombre de pixels à 1
    std::array<std::uint8_t, 3> seed_rgb{}; // couleur du pixel cliqué
    std::array<std::uint8_t, 3> mean_rgb{}; // couleur moyenne de la région (couleur de fil proposée)
    int x0{0}, y0{0}, x1{-1}, y1{-1}; // boîte englobante (bornes incluses)

    [[nodiscard]] bool empty() const { return pixel_count == 0; }
    [[nodiscard]] bool contains(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height &&
               mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x)] != 0;
    }
};

// Région connexe de couleur proche contenant le pixel (`seed_x`, `seed_y`). Erreur `UserInput` si
// le pixel est hors de l'image ou transparent (alpha < 128, le fond). Déterministe : parcours en
// largeur à ordre fixe, aucun conteneur non ordonné.
[[nodiscard]] Result<GrownRegion> grow_region(const image::Image& img, int seed_x, int seed_y,
                                              const GrowOptions& options = {});

// Segmentation d'une seule région (identifiant 1, couleur = couleur moyenne) dont les pixels sont
// ceux du masque, tout le reste étant le fond (label 0). Même grille que l'image : tous les
// consommateurs de `Segmentation` (vectorisation, auto-numérisation) s'appliquent tels quels.
// Segmentation vide (aucune région) si le masque est vide.
[[nodiscard]] Segmentation segmentation_from_grown(const GrownRegion& grown);

// Image RGBA de l'aperçu : pixels de la région teintés de `tint` (alpha `alpha`), le reste
// transparent. Même taille que l'image.
[[nodiscard]] image::Image render_grown_preview(const GrownRegion& grown,
                                                std::array<std::uint8_t, 3> tint,
                                                std::uint8_t alpha = 140);

} // namespace openstitch::segmentation
