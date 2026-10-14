// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <vector>

#include "openstitch/document/finishing.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_generation {

// « Optimiser les trajets », niveau 3 : ordre et sens des passes de sous-couche d'un même objet.
//
// La sous-couche est cousue AVANT le remplissage et recouverte par lui : l'ordre dans lequel on
// enchaîne ses passes, et le sens de chacune, ne changent pas le résultat visible. Un groupe de
// passes consécutives (même objet, uniquement des points de passe `Underlay`, séparées par de
// simples sauts) est donc réordonné et retourné pour minimiser les déplacements, en comptant une
// coupe (déplacement au-delà de `trim_threshold`) comme très coûteuse. Les points eux-mêmes ne
// sont ni déplacés, ni ajoutés, ni retirés ; la couche supérieure n'est jamais touchée. Un groupe
// n'est modifié que si le coût baisse strictement (idempotent). Fonction pure et déterministe :
// permutations essayées dans l'ordre lexicographique, ou plus proche voisin au-delà de 6 passes.
struct UnderlayOrderResult {
    std::vector<stitch::StitchCommand> commands;
    std::size_t blocks{0}; // groupes effectivement réordonnés
    double saved_um{0.0};  // somme des longueurs de saut économisées
};

[[nodiscard]] UnderlayOrderResult
reorder_underlay_passes(const std::vector<stitch::StitchCommand>& cmds,
                        const document::SequenceFinishing& finishing);

} // namespace openstitch::stitch_generation
