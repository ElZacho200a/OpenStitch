// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"

namespace openstitch::autodigitize {

// FONDU DE COULEURS À DEUX FILS (cf. docs/source/directional-fill.md, « Fondu »).
//
// Une région dont les couleurs s'étalent entre deux teintes est cousue en deux
// passes : un fond uni de la couleur s1, puis un remplissage de la couleur s2
// dont l'écart entre lignes varie. Là où les lignes de s2 sont serrées, la
// teinte visible tend vers s2 ; là où elles sont espacées, le fond s1 domine.
// Si `b` est la largeur du fil de s2 et `1/rho` l'écart entre ses lignes, une
// période de 1/rho montre une largeur b de s2 et 1/rho - b de s1 : la
// proportion de s2 est t = b / (1/rho) = b * rho, d'où rho = t / b et un écart
// 1/rho = b / t (Liu et al., CGF 2023, §4.3).

// Un échantillon de l'image : position (µm, repère du modèle) et couleur sRGB.
struct ColorSample {
    Vec2um pos{};
    std::array<std::uint8_t, 3> rgb{};
};

struct TwoColorBlendOptions {
    Micrometers thread_width{400};  // `b` : largeur visible du fil de s2 (0,4 mm)
    Micrometers min_spacing{400};   // écart le plus serré autorisé (t = 1)
    Micrometers max_spacing{4'000}; // écart le plus lâche (t -> 0)
    // Quantiles écartés de chaque côté de l'axe de couleurs avant de fixer s1 et
    // s2 (valeurs aberrantes : reflets, pixels isolés).
    double outlier_fraction{0.025};
    // Variation minimale de t sur la région pour parler de dégradé ; en deçà,
    // la densité est uniforme (axe nul).
    double min_ramp{0.05};
};

struct TwoColorBlend {
    std::array<std::uint8_t, 3> background{}; // s1 : fond uni, couleur la mieux représentée
    std::array<std::uint8_t, 3> foreground{}; // s2 : couleur du remplissage à densité variable
    double mean_t{0.0};                       // proportion moyenne de s2 dans la région, [0 ; 1]
    // Densité du fil s2 : axe et écarts de début/fin. Axe nul (`from == to`) si
    // la région n'a pas de dégradé net : écart uniforme selon `mean_t`.
    document::DensityGradient foreground_density;
};

// Analyse `samples` : ACP des couleurs en CMY (première composante), choix de
// s1/s2 aux extrémités de l'axe après rejet des valeurs aberrantes, puis
// ajustement d'un plan t(x, y) par moindres carrés. Déterministe (aucune
// graine, itération en ordre d'entrée). nullopt si moins de 2 échantillons ou
// si toutes les couleurs sont identiques (rien à fondre).
[[nodiscard]] std::optional<TwoColorBlend>
analyze_two_color_blend(const std::vector<ColorSample>& samples,
                        const TwoColorBlendOptions& options = {});

} // namespace openstitch::autodigitize
