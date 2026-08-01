// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_generation {

// État dérivé d'un objet retouché (ADR-014) — jamais stocké, toujours
// recalculé depuis `overrides`/`edited_fingerprint`/`edited_point_count` et la
// vue brute ACTUELLE de l'objet (cadrage Lot 8 §1).
enum class ObjectEditState : std::uint8_t { Clean, ManuallyEdited, Dirty };

// Vue brute d'un objet dans une séquence déjà générée : toutes les commandes
// portant `object` comme source, dans l'ordre où `generate_sequence` les a
// produites (toutes passes confondues — Underlay/TopStitch/Travel/Lock), qu'elles
// soient contiguës (cas général) ou entrelacées (satin routé, trajets cachés
// entre colonnes). Domaine indexé par `document::StitchOverride::base_index`.
[[nodiscard]] std::vector<stitch::StitchCommand> raw_slice(const stitch::StitchSequence& full,
                                                             ObjectId object);

// Empreinte 64 bits (FNV-1a) déterministe et reproductible bit à bit sur une
// vue brute : dépend explicitement de la position, du type de commande, de la
// passe, de la source et de l'ordre de chaque commande (sérialisation
// little-endian de champs à largeur fixe -> pas d'ambiguïté d'encodage).
[[nodiscard]] std::uint64_t fingerprint(const std::vector<stitch::StitchCommand>& raw_slice);

// Classe l'état d'un objet à partir de sa vue brute ACTUELLE (recalcule
// toujours ; aucun état stocké ni mis en cache).
[[nodiscard]] ObjectEditState classify_edit_state(const document::EmbroideryObject& object,
                                                   const std::vector<stitch::StitchCommand>& raw);

// Applique en place, sur `sequence` (déjà produite par `generate_sequence`),
// les retouches des objets `ManuallyEdited` de `project`. Les objets `Dirty`
// restent inchangés (séquence brute conservée) ; leurs identifiants sont
// retournés, triés pour un résultat déterministe. N'a aucun effet observable
// si aucun objet n'a d'`overrides` (rétrocompatibilité, cf. cadrage Lot 8 §1).
// Chaque champ d'un `StitchOverride` est validé indépendamment contre la
// cible : `moved_to` exige une entrée TopStitch de type `Stitch` ; `forced_type`
// (Stitch<->Jump, bidirectionnel) et `trim_after` acceptent une entrée
// TopStitch de type `Stitch` OU `Jump`. Un champ invalide pour sa cible est
// ignoré silencieusement, sans rejeter les autres champs du même override.
[[nodiscard]] std::vector<ObjectId> apply_manual_overrides(stitch::StitchSequence& sequence,
                                                            const document::Project& project);

// Point d'entree UNIQUE pour tout consommateur de production (desktop, CLI,
// export, simulation) : enchaine generate_sequence + apply_manual_overrides.
// Signature volontairement identique a generate_sequence(project) (cadrage
// Lot 8 SS5) pour que les appelants existants n'aient qu'a substituer l'appel.
// `generate_sequence` et `apply_manual_overrides` restent des blocs de
// construction internes, appelables separement en test/generateur -- mais
// aucun consommateur de production ne doit composer les deux passes
// lui-meme (voir tests/check_no_raw_sequence_bypass.cmake).
[[nodiscard]] Result<stitch::StitchSequence> effective_sequence(const document::Project& project);

}  // namespace openstitch::stitch_generation
