// SPDX-License-Identifier: Apache-2.0
#pragma once

// Textures procédurales du rendu réaliste, générées sur CPU, SANS Qt : pixels RGBA8 prémultipliés.
// Aucun shader personnalisé (ni qsb ni Qt6ShaderTools disponibles) : on s'appuie sur les matériaux
// de texture standard de Qt Quick. `ThreadMaterialProvider` est la couture où brancher plus tard
// des shaders si le module qtshadertools est ajouté.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "realistic_scene.hpp"

namespace openstitch::desktop_v2::studio {

struct PixelImage {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba; // prémultiplié, ligne 0 = haut

    [[nodiscard]] const std::uint8_t* pixel(int x, int y) const {
        return rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                              static_cast<std::size_t>(x)) *
                                 4;
    }
};

inline constexpr int kSpriteCellWidth = 24;  // pixels en travers du fil
inline constexpr int kSpriteHeight = 32;     // pixels le long du fil
inline constexpr int kFabricTilePixels = 64; // côté de la tuile de tissage

// Sprite de fil d'une couleur : atlas kOrientationBins cellules de kSpriteCellWidth x
// kSpriteHeight. Éclairage cylindrique en travers (même modèle que libs/stitch_render/src/
// raster.cpp : lumière haut-gauche, assombrissement des bords, reflet pow 28), torsion le long,
// bords adoucis et bouts effacés en alpha.
[[nodiscard]] PixelImage generateThreadSprite(std::array<std::uint8_t, 3> rgb,
                                              const RealisticStyle& style);
// Ombre portée : silhouette du fil, noir translucide (alpha = intensité d'ombre).
[[nodiscard]] PixelImage generateShadowSprite(const RealisticStyle& style);
// Tuile de tissage opaque (période kFabricTileMm), à répéter.
[[nodiscard]] PixelImage generateFabricTile(const RealisticStyle& style);

// Cache par couleur : une petite image par couleur de fil, générée une seule fois tant que le
// style (relief, brillance, torsion) ne change pas. `generatedCount` permet de le vérifier.
class ThreadSpriteCache {
public:
    [[nodiscard]] const PixelImage& sprite(std::array<std::uint8_t, 3> rgb);
    // Vide le cache si le style visuel du fil a changé ; faux sinon (rien n'est régénéré).
    bool setStyle(const RealisticStyle& style);
    [[nodiscard]] const RealisticStyle& style() const { return style_; }
    [[nodiscard]] std::size_t size() const { return sprites_.size(); }
    [[nodiscard]] std::size_t generatedCount() const { return generated_; }

private:
    RealisticStyle style_;
    std::map<std::uint32_t, PixelImage> sprites_; // ordonné : pas d'ordre qui fuit
    std::size_t generated_{0};
};

} // namespace openstitch::desktop_v2::studio
