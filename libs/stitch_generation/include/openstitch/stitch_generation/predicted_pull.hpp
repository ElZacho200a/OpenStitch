// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/pull_field.hpp"

namespace openstitch::stitch_generation {

// Résout les catégories de matière et de stabilisateur persistées dans le projet.
// Profils exploratoires NON calibrés : matière inconnue = réponse inchangée ;
// une réponse explicitement calibrée n'est jamais écrasée par ces heuristiques.
[[nodiscard]] FabricResponse predicted_fabric_response(const document::Project& project,
                                                       const FabricResponse& base = FabricResponse{});

// Passe finale du tirage de NIVEAU 3 (`PullMode::Predicted`), appelée par `effective_sequence`
// APRÈS les retouches manuelles et AVANT les finitions (coupes, verrous : ils se posent sur la
// matière déjà compensée).
//
// Les sources du champ sont TOUS les segments cousus de la séquence (deux pénétrations
// consécutives d'un même objet, sous-couche comprise) : les objets voisins se tirent
// mutuellement, ce que les niveaux 1 et 2 ignorent. Seuls les points de couche supérieure des
// objets en mode `Predicted` sont déplacés (de `PullField::compensation_at`, calculé sur les
// positions d'origine). Les poids des segments tiennent compte des passes, de la longueur
// et du type de remplissage ; la matière et l'entoilage viennent du projet.
// Sans aucun objet `Predicted`, la séquence est rendue strictement inchangée.
// Fonction pure et déterministe.
void apply_predicted_pull(stitch::StitchSequence& sequence, const document::Project& project,
                          const FabricResponse& fabric = FabricResponse{});

} // namespace openstitch::stitch_generation
