// SPDX-License-Identifier: Apache-2.0
// En-tête interne à libs/auto_satin : échantillonnage d'un axe en traversées
// satin (specs/plans/satin-squelette-traversees.md, §6).
//
// Pour chaque échantillon P(s) de l'axe : orientation g(s), droite orientée
// P + t·u(g), corde = intervalle intérieur contenant P. Le pas entre
// échantillons est piloté par l'espacement au bord le plus écarté de la corde
// (et non par l'axe), de sorte qu'aucun trou n'apparaît du côté extérieur d'un
// virage ; le côté intérieur, plus dense, relève des points courts.
#pragma once

#include <vector>

#include "axis.hpp"
#include "chord.hpp"
#include "orientation.hpp"

namespace openstitch::auto_satin::detail {

struct SamplerParams {
    double spacing_um{400.0};      // ρ : espacement cible au bord le plus écarté
    double h_min_um{50.0};         // pas minimal sur l'axe
    double h_max_ratio{3.0};       // pas maximal = ratio × ρ
    double min_chord_um{300.0};    // sous cette longueur, la corde n'est pas émise
    double min_sin{0.17};          // |sin(g − α)| minimal (≈ 10°) : jamais de corde ∥ à l'axe
    double tolerance_um{60.0};     // écart toléré d'un échantillon au bord (raster ≠ polygone)
    double max_extension_um{6000}; // plafond du prolongement de chaque bout
    double radius_guard{2.5};      // chorde > garde × rayon inscrit : diagnostic
};

// Une traversée calculée. `a` est du côté droit de l'axe (−n), `b` du côté gauche.
struct AxisSample {
    double s{0.0};     // abscisse sur l'axe (peut sortir de [0, L] aux bouts prolongés)
    P2 p{};            // point de l'axe
    double alpha{0.0}; // tangente
    double g{0.0};     // orientation de la corde (modulo π)
    P2 a{};
    P2 b{};
    double length{0.0};
};

struct SamplerDiagnostics {
    int outside_samples{0};   // échantillon hors région, abandonné
    int too_short{0};         // corde < min_chord_um, non émise
    int clamped_angle{0};     // g ramené pour respecter min_sin
    int radius_guard_hits{0}; // corde anormalement longue pour le rayon inscrit
};

struct SamplerResult {
    std::vector<AxisSample> samples;
    SamplerDiagnostics diagnostics;
    double s_begin{0.0}; // abscisse du premier échantillon (≤ 0 : bout prolongé)
    double s_end{0.0};   // abscisse du dernier (≥ L)
};

// Échantillonne l'axe. Déterministe. `polys` est la région (extérieur + trous).
[[nodiscard]] SamplerResult sample_axis(const Axis& axis, const std::vector<Poly>& polys,
                                        const OrientationKeys& keys, const SamplerParams& params);

} // namespace openstitch::auto_satin::detail
