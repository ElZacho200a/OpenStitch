// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch/color_block.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_analysis {

// AI-03a (AD-02 bis) : dérive de (projet, séquence) la table ORDONNÉE des
// blocs de couleur du design -- UNE seule dérivation, réutilisée par l'export
// (S2b/S2c), le film couleur (S4) et la fiche de production (S12), jamais une
// seconde logique « couleurs du design » (AD-02 bis).
//
// Un bloc commence au premier `Stitch`/`Jump` suivant un `ColorChange`/`Stop`
// (ou au tout début de la séquence) et se termine juste avant le
// `ColorChange`/`Stop`/`End` suivant. La couleur et l'identité de fil
// viennent de l'objet source (`EmbroideryObject`) de la PREMIÈRE commande du
// bloc ; `thread_key` reste vide en P0 (renseigné par S4 depuis le fil de
// l'objet, sans changer le type, AD-02 bis). Un bloc dont aucun objet source
// n'est trouvable (`source == ObjectId{}`, typiquement un design importé --
// DST ne porte aucune vraie couleur, roadmap §2 FMT-002 -- ou tout autre
// identifiant orphelin) reçoit une couleur par défaut NOIRE, `thread_key`
// vide : une couleur inconnue honnête, jamais une supposition. Les blocs
// vides (deux arrêts consécutifs sans point entre eux) sont omis.
[[nodiscard]] std::vector<stitch::ColorBlock> color_blocks(const document::Project& project,
                                                           const stitch::StitchSequence& sequence);

} // namespace openstitch::stitch_analysis
