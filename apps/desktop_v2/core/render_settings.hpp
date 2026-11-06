// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- réglages du rendu réaliste de l'aperçu (qualité, résolution, fil, tissu). Sans Qt.
// Le calcul vit dans libs/stitch_render ; ceci ne porte que l'état utilisateur, borné et
// sérialisable (clé=valeur, relu sans jamais échouer : une valeur invalide retombe sur le défaut de
// CETTE clé).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openstitch::desktop_v2 {

struct RenderSettings {
    double pxPerMm{3.0};        // résolution de l'aperçu : [1 ; 12] px/mm
    double threadWidthMm{0.35}; // épaisseur apparente du fil : [0,1 ; 1]
    double relief{0.7};         // relief du fil [0 ; 1]
    double sheen{0.5};          // brillance [0 ; 1]
    double twist{0.5};          // torsion visible [0 ; 1]
    double shadow{0.5};         // ombre portée [0 ; 1]
    std::array<std::uint8_t, 3> fabricRgb{245, 243, 238};
    bool fabricWeave{false};  // faux : toile unie ; vrai : armure visible
    double fabricRelief{0.4}; // relief de l'armure [0 ; 1]
    bool highQuality{true};   // faux : moteur rapide (brouillon)

    bool operator==(const RenderSettings&) const = default;
};

inline constexpr double kMinPxPerMm = 1.0;
inline constexpr double kMaxPxPerMm = 12.0;

// Copie bornée (résolution, épaisseur, intensités).
[[nodiscard]] RenderSettings clamped(RenderSettings settings);

// Préréglages nommés : "draft", "balanced", "high", "photo".
[[nodiscard]] std::optional<RenderSettings> renderPreset(std::string_view id);
[[nodiscard]] std::string renderPresetLabel(std::string_view id); // libellé français
// Identifiant du préréglage qui correspond EXACTEMENT aux réglages, sinon vide (« personnalisé »).
[[nodiscard]] std::string matchingRenderPreset(const RenderSettings& settings);

// Lignes `render.<clé>=<valeur>` ; `renderSettingsApply` lit une clé (sans le préfixe `render.`) et
// renvoie faux si la clé est inconnue ou la valeur invalide (réglages inchangés).
[[nodiscard]] std::string renderSettingsToText(const RenderSettings& settings);
bool renderSettingsApply(RenderSettings& settings, std::string_view key, std::string_view value);

} // namespace openstitch::desktop_v2
