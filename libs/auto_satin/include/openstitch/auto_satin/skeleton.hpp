// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/auto_satin/raster.hpp"

namespace openstitch::auto_satin {

// Amincit le masque binaire jusqu'à une largeur d'un pixel (squelette), par
// l'algorithme de Zhang-Suen (méthode publiée classique, réimplémentée).
// Déterministe. Le résultat est un RasterMask (1 = pixel de squelette).
[[nodiscard]] RasterMask thin_zhang_suen(const RasterMask& mask);

}  // namespace openstitch::auto_satin
