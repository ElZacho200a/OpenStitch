// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::document {

// Taille et position physiques d'une image matricielle sur le canevas.
// Le lien pixels <-> millimètres est TOUJOURS explicite : il naît ici,
// au moment de l'import, et nulle part ailleurs.
struct ImagePlacement {
    Micrometers width{};    // largeur physique de l'image
    Micrometers height{};   // hauteur physique de l'image
    Vec2um center{};        // centre de l'image, relatif au centre du canevas
};

// Placement en imposant la largeur, ratio conservé.
[[nodiscard]] Result<ImagePlacement> placement_from_width(int width_px, int height_px,
                                                          Millimeters target_width);

// Placement en imposant largeur et hauteur (ratio libre).
[[nodiscard]] Result<ImagePlacement> placement_from_size(int width_px, int height_px,
                                                         Millimeters target_width,
                                                         Millimeters target_height);

// Placement depuis une résolution en points par pouce (usage : valeur par défaut).
[[nodiscard]] Result<ImagePlacement> placement_from_dpi(int width_px, int height_px, double dpi);

// Échelle résultante d'un placement (millimètres par pixel, axe X).
[[nodiscard]] Millimeters mm_per_pixel(const ImagePlacement& placement, int width_px);

}  // namespace openstitch::document
