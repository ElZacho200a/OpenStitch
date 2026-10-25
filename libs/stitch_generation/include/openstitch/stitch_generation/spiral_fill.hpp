// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation {

// Remplissage en SPIRALE (HP-STI-009) d'une région (contour extérieur + trous, polygones
// aplatis) : spirale d'Archimède r = row_spacing·θ / 2π depuis un centre (`params.center`, sinon
// le milieu de la boîte englobante) jusqu'au point le plus éloigné du contour, rognée à la région
// aux points exacts d'entrée et de sortie. Dans une région étoilée par rapport au centre la
// couture est UN SEUL tracé continu, sans saut ; une sortie de la région (forme non convexe, trou)
// est reliée par une liaison cousue si elle reste dans la région, sinon par un saut.
//
// Pas angulaire : le point suit la spirale par cordes d'au plus `stitch_length` (bornée à [1 ; 7]
// mm, pas angulaire <= 0,35 rad) ; les points plus proches que `min_length` du précédent sont
// omis (les points de rognage au bord et le dernier ne le sont pas). Espacement borné à
// [0,1 ; 10] mm. Fonction pure et déterministe.
[[nodiscard]] std::vector<FillStitch> fill_spiral(const geometry::PathSet& region,
                                                  const document::SpiralFillParams& params);

} // namespace openstitch::stitch_generation
