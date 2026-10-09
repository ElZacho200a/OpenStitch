// SPDX-License-Identifier: Apache-2.0
// En-tête interne à libs/auto_satin : axe de référence lissé du moteur
// d'auto-satin par squelette (specs/plans/satin-squelette-traversees.md, §3, étape 1).
//
// Le squelette de Zhang-Suen est une chaîne de pixels : sa longueur d'arc est
// surestimée (marches en 1/√2) et sa tangente est du bruit à l'échelle du pas.
// L'axe est donc ré-échantillonné à pas fixe, lissé par moyenne mobile
// SYMÉTRIQUE tronquée (les extrémités restent fixes), puis paramétré par la
// longueur d'arc du résultat. La tangente se mesure sur une fenêtre `w_t`
// indépendante du pas d'échantillonnage des traversées.
#pragma once

#include <vector>

#include "geometry_detail.hpp"

namespace openstitch::auto_satin::detail {

struct AxisParams {
    double resample_step_um{50.0};   // pas du rééchantillonnage uniforme initial
    double smooth_radius_um{250.0};  // demi-fenêtre de la moyenne mobile
    int smooth_passes{2};            // deux passes ≈ noyau triangulaire
    double tangent_window_um{600.0}; // fenêtre de mesure de la tangente
};

class Axis {
public:
    Axis() = default;

    // Construit l'axe depuis une polyligne brute (≥ 2 points distincts). Axe vide
    // (`empty()`) si la polyligne est dégénérée.
    static Axis build(const std::vector<P2>& raw, const AxisParams& params);

    [[nodiscard]] bool empty() const { return points_.size() < 2; }
    [[nodiscard]] double length() const { return empty() ? 0.0 : cumulative_.back(); }
    [[nodiscard]] const std::vector<P2>& points() const { return points_; }

    // Position à l'abscisse s ∈ [0, L] (interpolation linéaire ; s est borné).
    [[nodiscard]] P2 position(double s) const;

    // Angle de la tangente (radians, direction des s croissants) à l'abscisse s,
    // mesuré sur la fenêtre `tangent_window_um` (réduite près des extrémités,
    // jamais à moins d'un pas). Pour s hors de [0, L], valeur de l'extrémité.
    [[nodiscard]] double alpha(double s) const;

private:
    std::vector<P2> points_;
    std::vector<double> cumulative_;
    double tangent_window_um_{600.0};
};

} // namespace openstitch::auto_satin::detail
