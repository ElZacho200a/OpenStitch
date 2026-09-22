// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Axe principal d'une région pleine (extérieur moins trous), tiré de ses
// moments d'inertie du second ordre (formule de Green sur les contours
// aplatis -- exacte pour un polygone, sans rasterisation).
struct PrincipalAxis {
    // Direction de plus grande extension, dans [0, pi) (repère Y vers le haut).
    Angle angle{};
    // Rapport des moments principaux (max/min, >= 1). Proche de 1 : forme
    // quasi isotrope (disque, carré) dont l'axe n'a pas de sens stable.
    double anisotropy{1.0};
    double area_um2{0.0};
};

// Axe principal de l'union des `sets`. nullopt si l'aire nette est nulle.
// Déterministe (somme dans l'ordre des contours).
[[nodiscard]] std::optional<PrincipalAxis> principal_axis(const std::vector<PathSet>& sets);

} // namespace openstitch::geometry
