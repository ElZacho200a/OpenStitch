// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation {

// Remplissage concentrique / contour (« echo », HP-STI-007) d'une région (contour extérieur +
// trous, polygones déjà aplatis) : anneaux successifs décalés de `row_spacing` vers l'intérieur
// (Clipper2, via `geometry::inset_path_set`), de l'extérieur vers le centre. Chaque trou de la
// région est contourné par ses propres anneaux, qui grandissent à mesure que la couture s'éloigne
// du bord ; une région qui se scinde en plusieurs îlots poursuit chaque îlot à son tour.
//
// Avec `spiral`, chaque tour glisse progressivement de son anneau vers l'anneau suivant : la
// couture est une spirale continue, sans saut de tour à tour. Sinon les anneaux sont fermés et
// reliés par un court pas.
//
// Longueurs de point : au plus `stitch_length` (bornée à [1 ; 7] mm) ; au moins `min_length`
// (bornée à [0 ; stitch_length]) hors coins. Une liaison plus longue qu'un point est subdivisée
// (jusqu'à deux longueurs) ou devient un saut (`FillStitch::jump`). Espacement borné à
// [0,1 ; 10] mm. Fonction pure et déterministe : mêmes entrées, mêmes points.
[[nodiscard]] std::vector<FillStitch> fill_contour(const geometry::PathSet& region,
                                                   const document::ContourFillParams& params);

} // namespace openstitch::stitch_generation
