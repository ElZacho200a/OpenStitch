// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/auto_satin/skeleton_satin.hpp"

namespace openstitch::auto_satin {

// Satin guide : les traversées sont les cordes du contour perpendiculaires à
// une ligne centrale modifiable (AutoSatinGuide::path). Sans chemin manuel, une
// ligne centrale initiale est proposée selon l'axe principal de la forme.
// Ce moteur est indépendant du balayage parallèle et du squelette raster.
// Les trous et les discontinuités cassent la colonne, sans saut à travers le vide.
[[nodiscard]] Result<SkeletonSatinResult>
generate_guided_satin(const geometry::PathSet& region, const SkeletonSatinParameters& params);

} // namespace openstitch::auto_satin
