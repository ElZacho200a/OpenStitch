// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_analysis {

// HP-THR-012 : objet de broderie dont un point cousu passe à moins de `tolerance` de `pos`.
// Parcourt la séquence EFFECTIVE (passer `effective_sequence`, jamais la séquence brute) ; les
// segments sont les liaisons entre deux piqûres consécutives (sauts, coupes et déplacements
// cachés exclus). Plusieurs objets se recouvrent : celui cousu EN DERNIER (donc visible au
// dessus) l'emporte. Déterministe ; `nullopt` si aucun point n'est assez proche ou si la
// commande n'a pas d'objet source (design importé, point manuel).
[[nodiscard]] std::optional<ObjectId> stitch_object_at(const stitch::StitchSequence& sequence,
                                                       Vec2um pos, Micrometers tolerance);

} // namespace openstitch::stitch_analysis
