// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>

namespace openstitch::stitch_render {

// Qualité du rendu réaliste : « Fast » sacrifie torsion, ombre portée et
// texture fine du tissu pour rester fluide sur les très gros motifs.
enum class Quality : std::uint8_t {
    Fast,
    High,
};

// Texture procédurale du tissu de fond.
enum class FabricTexture : std::uint8_t {
    Plain, // aplat de couleur
    Weave, // armure toile (chaîne/trame)
    Felt,  // feutrine : grain irrégulier
};

// Réglages du rendu réaliste (façon « TrueView »). Toutes les intensités sont
// dans [0, 1] ; `sanitized()` ramène toute valeur hors plage dans les bornes.
struct RenderParams {
    // Diamètre apparent du fil, en millimètres (un fil 40 wt fait ~0,3 mm).
    double thread_width_mm{0.35};
    // Relief du fil : amplitude de l'ombrage cylindrique le long de sa section.
    double relief{0.7};
    // Brillance (reflet spéculaire) du fil.
    double sheen{0.5};
    // Visibilité de la torsion (stries obliques le long du fil).
    double twist{0.5};
    // Intensité de l'ombre portée du fil sur le tissu et les points dessous.
    double shadow{0.5};
    // Tissu de fond.
    std::array<std::uint8_t, 3> fabric_rgb{240, 236, 228};
    // Faux : le fond reste transparent (le calque image reste visible dessous).
    bool fabric_opaque{true};
    FabricTexture fabric_texture{FabricTexture::Weave};
    // Relief de la texture du tissu.
    double fabric_relief{0.4};
    Quality quality{Quality::High};

    bool operator==(const RenderParams&) const = default;
};

inline constexpr double kMinThreadWidthMm = 0.1;
inline constexpr double kMaxThreadWidthMm = 1.0;

// Copie bornée : épaisseur dans [kMin, kMax], intensités dans [0, 1].
[[nodiscard]] RenderParams sanitized(RenderParams params);

// Condensat stable (FNV-1a) des réglages : sert de clé de cache.
[[nodiscard]] std::uint64_t hash_params(const RenderParams& params);

} // namespace openstitch::stitch_render
