// SPDX-License-Identifier: Apache-2.0
// En-tête interne à libs/auto_satin : mesure de couverture partagée par les stratégies.
#pragma once

#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin::detail {

// Couverture estimée : rasterise les triangles balayés par deux traversées consécutives d'une
// même colonne (rendu des fils) sur le masque de la région, et remplit les diagnostics de
// couverture, de recouvrement, de pic de densité et de localisation des manques.
void measure_coverage(const geometry::PathSet& region, SkeletonSatinResult& result);

} // namespace openstitch::auto_satin::detail
