// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_generation {

// Cache de la séquence générée par objet indépendant (droit, tatami, directionnel) —
// HP-PERF-002. La séquence d'un tel objet est une fonction pure de (identifiant, paramètres,
// géométrie source) : le lien avec l'objet précédent se fait plus tard, à l'assemblage.
//
// Clé EXACTE : l'entrée mémorise une copie des paramètres et de la géométrie et les compare
// intégralement (operator== défaut des types du document). Aucune empreinte seule : un résultat
// servi du cache est identique octet pour octet à un recalcul, et un champ ajouté plus tard à
// un type de paramètres est comparé automatiquement.

// Séquence mémorisée pour cet objet si ses paramètres et sa géométrie sont inchangés ; nullptr
// sinon (ou cache désactivé).
[[nodiscard]] std::shared_ptr<const stitch::StitchSequence>
chunk_cache_find(ObjectId id, const document::StitchParams& params,
                 const std::vector<geometry::PathSet>& paths);

// Mémorise la séquence de l'objet (remplace l'entrée précédente du même identifiant). Sans effet
// si le cache est désactivé.
void chunk_cache_store(ObjectId id, const document::StitchParams& params,
                       const std::vector<geometry::PathSet>& paths,
                       std::shared_ptr<const stitch::StitchSequence> chunk);

} // namespace openstitch::stitch_generation
