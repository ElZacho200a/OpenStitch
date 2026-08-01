// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <utility>
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

// `moved_to` (cadrage §2.1) : "uniquement des commandes Stitch" -- déplacer une
// position n'a de sens que pour un point de couture réel en passe TopStitch,
// pas pour un saut (dont la position est déterminée par ses deux voisins de
// trajet) ni pour un point de sous-couche/trajet caché. Exportée (au lieu de
// rester un détail de `apply_manual_overrides`) pour que l'UI (Lot 8.2,
// poignées de déplacement) n'ait pas à dupliquer cette règle d'éligibilité.
[[nodiscard]] bool is_movable_point(const stitch::StitchCommand& cmd);

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

// Vue complète d'un objet pour un consommateur UI qui a besoin à la fois de
// l'état dérivé (indicateur Clean/ManuallyEdited/Dirty), de la vue brute
// (positionnement des poignées d'édition) et du couple fingerprint/compteur
// exigé par les constructeurs de `MoveStitchPointCommand`/
// `SetStitchPointTypeCommand`/`SetStitchTrimCommand` (Lot 8.2) -- un seul
// appel à `generate_sequence` en interne, pour que l'appelant (desktop) n'ait
// jamais à l'invoquer lui-même (règle "aucune logique métier dans les
// widgets", cadrage §5).
struct ObjectEditView {
    ObjectEditState state{ObjectEditState::Clean};
    std::vector<stitch::StitchCommand> raw;  // raw_slice(objet), toutes passes confondues
    std::uint64_t fingerprint{0};
    std::uint32_t point_count{0};
};
[[nodiscard]] Result<ObjectEditView> edit_view(const document::Project& project, ObjectId object);

// État dérivé de chaque objet PORTANT des retouches (`overrides` non vide) du
// projet -- un seul appel à `generate_sequence` pour tout le document. Les
// objets Clean (cas le plus fréquent) n'apparaissent pas dans le résultat :
// leur état est implicitement Clean par absence. Sert aux indicateurs par
// objet (liste Document, inspecteur, Lot 8.2 §6) sans recalculer une vue
// brute complète pour des objets jamais retouchés.
[[nodiscard]] Result<std::vector<std::pair<ObjectId, ObjectEditState>>> classify_all_edit_states(
    const document::Project& project);

// Rafraîchissement UI consolidé (Lot 8.2, revue corrective point 1) : un
// SEUL appel à `generate_sequence` produit à la fois `effective` (contenu
// STRICTEMENT identique à `effective_sequence(project)` -- mêmes deux passes,
// generate_sequence puis apply_manual_overrides, jamais de raccourci qui
// omettrait les retouches), `edit_states` (équivalent de
// `classify_all_edit_states`) et, si `target` est fourni, `target_view`
// (équivalent de `edit_view(project, *target)`, y compris s'il est Dirty --
// l'appelant filtre comme il le ferait avec `edit_view`). Remplace la
// composition effective_sequence + classify_all_edit_states + edit_view
// (jusqu'à trois `generate_sequence` par rafraîchissement) par un seul,
// pour un appelant (desktop `refreshImage`) qui a besoin des trois à la fois.
struct RefreshContext {
    stitch::StitchSequence effective;
    std::vector<std::pair<ObjectId, ObjectEditState>> edit_states;
    std::optional<ObjectEditView> target_view;
};
[[nodiscard]] Result<RefreshContext> refresh_context(const document::Project& project,
                                                       std::optional<ObjectId> target);

}  // namespace openstitch::stitch_generation
