// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <vector>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_generation {

// « Optimiser les trajets », mode avancé et plus invasif : réordonner les régions (objets) d'une
// MÊME couleur. Désactivé par défaut (`HiddenTravel::reorder_regions`).
//
// Ce que ce mode préserve :
//  - jamais d'échange entre couleurs : seules les régions situées entre deux changements de fil
//    (ou arrêts) sont concernées ;
//  - l'ordre relatif de deux régions dont les emprises (points, élargis de la largeur de fil et
//    d'une marge) se touchent ou se recouvrent : c'est lui qui décide ce qui est cousu par-dessus ;
//  - chaque région est conservée intacte : mêmes points, même ordre, même sens (jamais de
//    retournement d'un remplissage) ;
//  - les régions verrouillées (`EmbroideryObject::locked`), celles qui portent un connecteur choisi
//    à la main et celles sans objet source restent à leur place et bornent les groupes.
//
// Dans ces limites, l'ordre est choisi pour raccourcir les déplacements : plus proche voisin sous
// contrainte d'ordre, puis déplacements de régions qui font strictement baisser le coût. Un groupe
// n'est modifié que si le coût baisse strictement (idempotent). Déterministe.
struct RegionOrderResult {
    std::vector<stitch::StitchCommand> commands;
    std::size_t groups{0};        // groupes réordonnés
    std::size_t regions_moved{0}; // régions dont la position a changé
    double saved_um{0.0};         // somme des longueurs de déplacement économisées
};

[[nodiscard]] RegionOrderResult
reorder_regions_same_color(const std::vector<stitch::StitchCommand>& cmds,
                           const document::Project& project,
                           const document::SequenceFinishing& finishing);

} // namespace openstitch::stitch_generation
