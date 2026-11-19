// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
#include <span>
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

// Vrai si les caches de régénération sont actifs (cf. set_chunk_cache_enabled) : le cache des
// routeurs de liaisons cachées (tatami.cpp) suit le même réglage.
[[nodiscard]] bool chunk_cache_enabled();

// Vide le cache des états de routeur (défini dans tatami.cpp) ; appelé par clear_chunk_cache.
void clear_router_cache();

// Vide le cache des testeurs de région de la fusion des points courts (length_bounds.cpp) ;
// appelé par clear_chunk_cache.
void clear_length_bounds_cache();

// Vide les caches du « knockout » (generate.cpp) ; appelé par clear_chunk_cache.
void clear_knockout_cache();
// --- Satins routés et auto-satin (HP-PERF-002, suite) -----------------------------------------
//
// Ces unités ne sont pas des fonctions pures de leur seul objet : elles lisent la DERNIÈRE
// commande déjà émise (position, et type pour décider d'un saut de tête ; absence = début de
// séquence). Aucune autre lecture de la séquence précédente (vérifié dans generate_satin,
// generate_auto_satin et generate_satin_group). Le résultat est donc une fonction pure de
// (identifiants, paramètres EFFECTIFS -- point d'entrée automatique déjà injecté --, géométrie
// source de l'auto-satin, dernière commande). Le cache mémorise uniquement les commandes
// AJOUTÉES par l'unité ; la clé compare ces quatre éléments intégralement, sans empreinte.
// Même budget, même LRU et même mutex que le cache des objets indépendants.
struct RoutedMember {
    ObjectId id;
    const document::StitchParams* params{nullptr};
};

struct RoutedTail {
    Vec2um pos{};
    stitch::CommandType type{stitch::CommandType::Stitch};

    bool operator==(const RoutedTail&) const = default;
};

[[nodiscard]] std::shared_ptr<const stitch::StitchSequence>
routed_cache_find(std::span<const RoutedMember> members,
                  const std::vector<geometry::PathSet>* paths,
                  const std::optional<RoutedTail>& tail);

void routed_cache_store(std::span<const RoutedMember> members,
                        const std::vector<geometry::PathSet>* paths,
                        const std::optional<RoutedTail>& tail,
                        std::shared_ptr<const stitch::StitchSequence> added);

} // namespace openstitch::stitch_generation
