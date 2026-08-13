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

// Aire nette d'un PathSet (extérieur moins trous), en µm². Toujours >= 0.
[[nodiscard]] double path_set_area_um2(const PathSet& set);

// Intersection et différence booléennes NonZero entre deux listes de PathSet,
// TROUS COMPRIS DES DEUX CÔTÉS (contrairement à `subtract_polygons`, dont les
// `cutouts` sont de simples contours sans trou) -- nécessaire par exemple pour
// qu'une poche non couverte, elle-même entourée de matière couverte de tous
// côtés (un trou dans `b`), reste bien comptée comme manquante plutôt que
// silencieusement traitée comme couverte. Chaque élément du résultat est une
// composante connexe indépendante (issue directement du `PolyTree64` de
// Clipper2 -- aucun calcul de composantes connexes séparé n'est nécessaire).
// Renvoie une liste vide si le résultat est vide (aucun recouvrement pour
// l'intersection, `a` totalement recouvert par `b` pour la différence).
[[nodiscard]] Result<std::vector<PathSet>> intersect_polygons(const std::vector<PathSet>& a,
                                                               const std::vector<PathSet>& b);
[[nodiscard]] Result<std::vector<PathSet>> difference_polygons(const std::vector<PathSet>& a,
                                                                const std::vector<PathSet>& b);

}  // namespace openstitch::geometry
