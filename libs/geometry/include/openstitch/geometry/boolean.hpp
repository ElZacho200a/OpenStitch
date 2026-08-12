// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Soustrait l'union de `cutouts` de `base` (différence booléenne, Clipper2,
// règle NonZero -- robuste aux `cutouts` qui se chevauchent entre eux, ex.
// deux bandes de colonnes satin adjacentes qui partagent un barreau : sous
// une règle pair-impair, ce chevauchement se serait annulé et aurait laissé
// un trou parasite dans le résultat). Chaque `cutout` est un simple contour
// fermé (pas de trous) ; son orientation d'origine n'a pas d'importance, la
// fonction la normalise en interne avant l'opération. Renvoie `base`
// inchangée (dans une liste à un élément) si `cutouts` est vide ou ne
// recouvre rien de `base`.
[[nodiscard]] Result<std::vector<PathSet>> subtract_polygons(const PathSet& base,
                                                              const std::vector<Path>& cutouts);

}  // namespace openstitch::geometry
