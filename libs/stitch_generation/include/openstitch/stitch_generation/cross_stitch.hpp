// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation {

// Point de croix (HP-STI-012) d'une région : grille de cellules carrées de côté `cell` (borné à
// [1 ; 8] mm), tournée de `grid_angle` autour de l'origine du projet (deux objets voisins
// partagent donc la même grille). Une cellule est cousue si son CENTRE est dans la région (trous
// respectés, règle pair-impair). Les cellules contiguës d'une rangée forment un tronçon cousu en
// rangs : toutes les jambes « / » à l'aller, les « \ » au retour (croix complète) ; en demi-croix
// seules les jambes « / » sont cousues, les rangées alternant le sens. `passes` (borné à [1 ; 3])
// répète le tronçon. Une liaison entre deux tronçons est un saut (`FillStitch::jump`), sauf d'une
// rangée à la suivante quand elle ne dépasse pas deux cellules (pas cousu le long du bord des
// rangées). Fonction pure et déterministe ; tous les points sont des coins de cellule.
[[nodiscard]] std::vector<FillStitch> fill_cross_stitch(const geometry::PathSet& region,
                                                        const document::CrossStitchParams& params);

} // namespace openstitch::stitch_generation
