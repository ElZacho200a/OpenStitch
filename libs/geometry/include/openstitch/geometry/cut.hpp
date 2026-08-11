// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Découpe une région selon une ligne de coupe (deux points quelconques,
// prolongés très au-delà de la région pour garantir une traversée complète
// quels que soient les points fournis) -- façon "cut line" d'Ink/Stitch pour
// guider manuellement la décomposition d'une forme en colonnes satin, en
// complément (pas en remplacement) de la détection automatique de jonctions
// (`auto_satin::build_satin_columns`). Retire une fine bande (`cut_width`,
// quelques dizaines de µm par défaut : négligeable une fois cousu, mais
// garantit une vraie séparation TOPOLOGIQUE via une booléenne Clipper2,
// robuste au maillage discret -- une ligne infiniment fine laisserait parfois
// deux morceaux qui se retouchent en un point, toujours une seule composante
// connexe).
//
// Renvoie un morceau par composante connexe résultante : 1 seul morceau
// (la région inchangée, aux quelques µm de la bande près) si la ligne ne
// traverse pas réellement la région ou est dégénérée (A == B) -- à
// l'appelant de vérifier `size() >= 2` pour savoir si la coupe a réellement
// séparé quelque chose.
[[nodiscard]] Result<std::vector<PathSet>> cut_path_set(const PathSet& region, Vec2um a, Vec2um b,
                                                         Micrometers cut_width = Micrometers{20});

}  // namespace openstitch::geometry
