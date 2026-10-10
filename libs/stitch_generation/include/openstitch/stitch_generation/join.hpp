// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_generation {

// Entrée/sortie automatiques (HP-ENG-010). Le moteur choisit, pour chaque objet, le
// SENS de couture qui minimise les déplacements :
//   déplacement = |fin de l'objet précédent -> début| + |fin -> début de l'objet suivant|/2
//                 + sauts internes (entre la sous-couche et la couche supérieure).
// Seul le sens de parcours change (jamais la géométrie : les mêmes pénétrations sont
// cousues), donc la couverture, la densité et le déterminisme sont inchangés.

// Vrai si l'objet doit être orienté automatiquement : `JoinMode::Auto`, ou `Inherit`
// avec `project.finishing.auto_join`.
[[nodiscard]] bool join_active(const document::Project& project,
                               const document::EmbroideryObject& object);

// Réoriente un tronçon généré d'UN objet (contour, remplissage : séquence de tracés
// « Jump + points cousus », sous-couches d'abord puis couche supérieure). Les quatre
// combinaisons (sous-couche inversée ou non) x (couche supérieure inversée ou non) sont
// évaluées ; la combinaison naturelle gagne les égalités. Inversion d'un tracé : mêmes
// pénétrations en ordre inverse, saut d'arrivée sur la nouvelle première pénétration,
// passes de segment conservées. Un tronçon qui ne suit pas cette forme (commandes autres
// que Jump/Stitch, couches mélangées) est rendu inchangé.
// `previous_end` : dernière position cousue avant l'objet ; `next_start` : début naturel
// de l'objet suivant (estimation de la sortie).
[[nodiscard]] std::vector<stitch::StitchCommand>
orient_chunk(const std::vector<stitch::StitchCommand>& chunk, std::optional<Vec2um> previous_end,
             std::optional<Vec2um> next_start);

// Longueur totale des sauts (µm) d'une séquence : somme des |Jump - position précédente|.
// Mesure utilisée par les tests et la CLI pour comparer deux réglages.
[[nodiscard]] double total_jump_length_um(const stitch::StitchSequence& sequence);

} // namespace openstitch::stitch_generation
