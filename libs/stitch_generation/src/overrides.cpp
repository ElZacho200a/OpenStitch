// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/overrides.hpp"

#include <algorithm>
#include <unordered_map>

#include "openstitch/stitch_generation/generate.hpp"

namespace openstitch::stitch_generation {

namespace {

constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;

std::uint64_t fnv1a_byte(std::uint64_t hash, std::uint8_t byte) {
    hash ^= byte;
    hash *= kFnvPrime;
    return hash;
}

// Sérialisation little-endian explicite, indépendante de l'architecture hôte :
// l'empreinte reste reproductible bit à bit (cadrage Lot 8 §1).
std::uint64_t hash_u32(std::uint64_t hash, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        hash = fnv1a_byte(hash, static_cast<std::uint8_t>(v >> (8 * i)));
    }
    return hash;
}

std::uint64_t hash_u64(std::uint64_t hash, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        hash = fnv1a_byte(hash, static_cast<std::uint8_t>(v >> (8 * i)));
    }
    return hash;
}

// Filtre commun aux trois opérations MVP (cadrage Lot 8 §2, restriction
// d'ouverture) : seules les entrées de passe TopStitch sont éligibles.
// Underlay/Travel/Lock restent entièrement régénérées, non éditables.
bool is_topstitch_entry(const stitch::StitchCommand& cmd) {
    return cmd.pass == stitch::StitchPass::TopStitch;
}

// Un point de couture "réel" au sens du cadrage : Stitch ou Jump. Exclut
// explicitement un cas de bord vérifié dans le code : le `ColorChange` inséré
// par `generate_sequence` avant un objet porte `source = <objet suivant>` et
// hérite du défaut de structure `pass == TopStitch` (jamais positionné
// explicitement) -- ce n'est pas un point généré, il ne doit être la cible
// d'aucune des trois opérations.
bool is_stitch_or_jump(stitch::CommandType type) {
    return type == stitch::CommandType::Stitch || type == stitch::CommandType::Jump;
}

// `forced_type` (cadrage §2.2) : "seule transition permise : Stitch <-> Jump"
// -- bidirectionnel, donc éligible depuis une cible Stitch OU Jump (pas
// seulement Stitch, contrairement à `can_move`).
bool can_force_type(const stitch::StitchCommand& cmd) {
    return is_topstitch_entry(cmd) && is_stitch_or_jump(cmd.type);
}

// `trim_after` (cadrage §2.3) : insère une commande machine supplémentaire
// après le point, sans toucher à sa géométrie -- aucune restriction de type
// au-delà "point de couture réel" (Stitch ou Jump), comme `forced_type`.
bool can_trim_after(const stitch::StitchCommand& cmd) {
    return is_topstitch_entry(cmd) && is_stitch_or_jump(cmd.type);
}

stitch::CommandType to_command_type(document::StitchPointType t) {
    return t == document::StitchPointType::Jump ? stitch::CommandType::Jump
                                                 : stitch::CommandType::Stitch;
}

}  // namespace

bool is_movable_point(const stitch::StitchCommand& cmd) {
    return is_topstitch_entry(cmd) && cmd.type == stitch::CommandType::Stitch;
}

std::vector<stitch::StitchCommand> raw_slice(const stitch::StitchSequence& full, ObjectId object) {
    std::vector<stitch::StitchCommand> result;
    for (const auto& cmd : full.commands) {
        if (cmd.source == object) {
            result.push_back(cmd);
        }
    }
    return result;
}

std::uint64_t fingerprint(const std::vector<stitch::StitchCommand>& raw_slice) {
    std::uint64_t hash = kFnvOffset;
    for (const auto& cmd : raw_slice) {
        hash = hash_u32(hash, static_cast<std::uint32_t>(cmd.pos.x.value));
        hash = hash_u32(hash, static_cast<std::uint32_t>(cmd.pos.y.value));
        hash = fnv1a_byte(hash, static_cast<std::uint8_t>(cmd.type));
        hash = fnv1a_byte(hash, static_cast<std::uint8_t>(cmd.pass));
        hash = hash_u64(hash, cmd.source.value);
    }
    return hash;
}

ObjectEditState classify_edit_state(const document::EmbroideryObject& object,
                                     const std::vector<stitch::StitchCommand>& raw) {
    if (object.overrides.empty()) {
        return ObjectEditState::Clean;
    }
    if (raw.size() != object.edited_point_count || fingerprint(raw) != object.edited_fingerprint) {
        return ObjectEditState::Dirty;
    }
    return ObjectEditState::ManuallyEdited;
}

std::vector<ObjectId> apply_manual_overrides(stitch::StitchSequence& sequence,
                                              const document::Project& project) {
    std::vector<ObjectId> dirty;

    std::unordered_map<std::uint64_t, const document::EmbroideryObject*> edited;
    for (const auto& obj : project.embroidery_objects) {
        if (!obj.overrides.empty()) {
            edited.emplace(obj.id.value, &obj);
        }
    }
    if (edited.empty()) {
        return dirty;  // aucun objet retouché : aucun effet observable (cf. §1)
    }

    // Un seul passage sur la séquence pour regrouper les index par objet
    // retouché : coût O(commandes), pas O(commandes * objets retouchés).
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> indices_by_object;
    for (std::size_t i = 0; i < sequence.commands.size(); ++i) {
        const auto it = edited.find(sequence.commands[i].source.value);
        if (it != edited.end()) {
            indices_by_object[sequence.commands[i].source.value].push_back(i);
        }
    }

    // Insertions de Trim différées : appliquées après coup, triées par index
    // décroissant, pour qu'aucune insertion ne décale un index restant à traiter.
    struct TrimInsertion {
        std::size_t seq_index;
        Vec2um pos;
        ObjectId source;
    };
    std::vector<TrimInsertion> trims;

    static const std::vector<std::size_t> kEmptyIndices;

    for (const auto& [id_value, obj_ptr] : edited) {
        const document::EmbroideryObject& obj = *obj_ptr;
        const auto found = indices_by_object.find(id_value);
        const std::vector<std::size_t>& idxs =
            found != indices_by_object.end() ? found->second : kEmptyIndices;

        std::vector<stitch::StitchCommand> raw;
        raw.reserve(idxs.size());
        for (std::size_t i : idxs) {
            raw.push_back(sequence.commands[i]);
        }

        if (classify_edit_state(obj, raw) == ObjectEditState::Dirty) {
            dirty.push_back(obj.id);
            continue;  // jamais réappliqué : la séquence brute reste telle quelle
        }
        if (obj.overrides.empty()) {
            continue;  // Clean (garde défensive : `edited` ne devrait pas le contenir)
        }

        // Doublons d'overrides pour un même base_index : la dernière entrée du
        // vecteur l'emporte, en bloc (pas de fusion champ à champ). En usage
        // normal (commandes du Lot 8.1) un seul base_index n'apparaît qu'une
        // fois ; un doublon ne peut venir que d'une construction directe.
        std::unordered_map<std::size_t, const document::StitchOverride*> resolved;
        for (const auto& ov : obj.overrides) {
            resolved[ov.base_index] = &ov;
        }

        for (const auto& [base_index, ov] : resolved) {
            if (base_index >= idxs.size()) {
                continue;  // index invalide : ignoré, pas d'erreur (cœur pur)
            }
            const std::size_t seq_index = idxs[base_index];
            const stitch::StitchCommand original = sequence.commands[seq_index];
            // Chaque champ est validé indépendamment, y compris la passe
            // TopStitch (cadrage §2.1-§2.3) : un override combinant plusieurs
            // champs applique ceux qui sont valides pour la cible et ignore
            // silencieusement les autres, plutôt que de rejeter tout le bloc
            // sur une règle unique trop restrictive.
            stitch::StitchCommand& cmd = sequence.commands[seq_index];
            if (ov->moved_to && is_movable_point(original)) {
                cmd.pos = *ov->moved_to;
                cmd.pass = stitch::StitchPass::Manual;
            }
            if (ov->forced_type && can_force_type(original)) {
                cmd.type = to_command_type(*ov->forced_type);
                cmd.pass = stitch::StitchPass::Manual;
            }
            if (ov->trim_after && can_trim_after(original)) {
                trims.push_back({seq_index, cmd.pos, obj.id});
            }
        }
    }

    std::sort(trims.begin(), trims.end(), [](const TrimInsertion& a, const TrimInsertion& b) {
        return a.seq_index > b.seq_index;
    });
    for (const auto& t : trims) {
        sequence.commands.insert(
            sequence.commands.begin() + static_cast<std::ptrdiff_t>(t.seq_index) + 1,
            stitch::StitchCommand{t.pos, stitch::CommandType::Trim, t.source, stitch::StitchPass::Manual});
    }

    std::sort(dirty.begin(), dirty.end());  // résultat déterministe (ordre d'itération de la map non garanti)
    return dirty;
}

Result<stitch::StitchSequence> effective_sequence(const document::Project& project) {
    auto sequence = generate_sequence(project);
    if (!sequence) {
        return sequence;
    }
    // Liste des objets Dirty volontairement ignorée ici : effective_sequence
    // est un point d'entree "au mieux" (apercu/export/...), pas un canal de
    // notification -- un consommateur qui a besoin de detecter/signaler l'etat
    // Dirty doit appeler apply_manual_overrides directement (UI, hors Lot 8.1).
    (void)apply_manual_overrides(*sequence, project);
    return sequence;
}

// Corps de `edit_view`/`classify_all_edit_states`, factorisés pour opérer sur
// une séquence déjà générée : `refresh_context` les réutilise tels quels sur
// UNE séquence partagée, au lieu que chacun des trois consommateurs
// (effective/edit_states/target_view) appelle `generate_sequence` séparément.
namespace {

ObjectEditView edit_view_from_sequence(const document::Project& project, ObjectId object,
                                        const stitch::StitchSequence& sequence) {
    ObjectEditView view;
    view.raw = raw_slice(sequence, object);
    view.point_count = static_cast<std::uint32_t>(view.raw.size());
    view.fingerprint = fingerprint(view.raw);
    const auto* obj = project.findEmbroidery(object);
    view.state = obj != nullptr ? classify_edit_state(*obj, view.raw) : ObjectEditState::Clean;
    return view;
}

std::vector<std::pair<ObjectId, ObjectEditState>> classify_all_from_sequence(
    const document::Project& project, const stitch::StitchSequence& sequence) {
    std::vector<std::pair<ObjectId, ObjectEditState>> result;
    result.reserve(project.embroidery_objects.size());
    for (const auto& obj : project.embroidery_objects) {
        if (obj.overrides.empty()) {
            continue;  // Clean implicite : absent du resultat
        }
        result.emplace_back(obj.id, classify_edit_state(obj, raw_slice(sequence, obj.id)));
    }
    return result;
}

}  // namespace

Result<ObjectEditView> edit_view(const document::Project& project, ObjectId object) {
    auto sequence = generate_sequence(project);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    return edit_view_from_sequence(project, object, *sequence);
}

Result<std::vector<std::pair<ObjectId, ObjectEditState>>> classify_all_edit_states(
    const document::Project& project) {
    const bool any_edited = std::any_of(
        project.embroidery_objects.begin(), project.embroidery_objects.end(),
        [](const document::EmbroideryObject& o) { return !o.overrides.empty(); });
    if (!any_edited) {
        return std::vector<std::pair<ObjectId, ObjectEditState>>{};  // aucun objet retouche : aucun appel a generate_sequence necessaire
    }
    auto sequence = generate_sequence(project);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    return classify_all_from_sequence(project, *sequence);
}

Result<RefreshContext> refresh_context(const document::Project& project, std::optional<ObjectId> target) {
    auto sequence = generate_sequence(project);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    RefreshContext ctx;
    ctx.edit_states = classify_all_from_sequence(project, *sequence);
    if (target) {
        ctx.target_view = edit_view_from_sequence(project, *target, *sequence);
    }
    // Mêmes deux passes, dans le même ordre, que `effective_sequence` : ne
    // JAMAIS diverger de ce que produirait `effective_sequence(project)` pour
    // le contenu affiché/exporté (cf. tests/check_no_raw_sequence_bypass.cmake).
    (void)apply_manual_overrides(*sequence, project);
    ctx.effective = std::move(*sequence);
    return ctx;
}

}  // namespace openstitch::stitch_generation
