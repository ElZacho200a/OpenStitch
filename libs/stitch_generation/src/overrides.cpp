// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/overrides.hpp"

#include <algorithm>
#include <unordered_map>

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

// Cible éligible à une retouche en MVP (cadrage Lot 8 §2) : une entrée
// TopStitch de type Stitch. Dans le générateur actuel, une entrée `pass ==
// TopStitch` est toujours une commande `Stitch` (running/tatami/satin
// n'émettent jamais de Jump/ColorChange en TopStitch) -- sauf un cas de bord
// vérifié dans le code : le `ColorChange` inséré par `generate_sequence` avant
// un objet porte `source = <objet suivant>` et hérite du défaut de structure
// `pass == TopStitch` (jamais positionné explicitement). Ce contrôle exclut
// donc explicitement ce cas plutôt que de supposer que toute entrée TopStitch
// est un point de couture.
bool is_overridable_entry(const stitch::StitchCommand& cmd) {
    return cmd.pass == stitch::StitchPass::TopStitch && cmd.type == stitch::CommandType::Stitch;
}

stitch::CommandType to_command_type(document::StitchPointType t) {
    return t == document::StitchPointType::Jump ? stitch::CommandType::Jump
                                                 : stitch::CommandType::Stitch;
}

}  // namespace

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
            if (!is_overridable_entry(original)) {
                continue;  // cible hors TopStitch/Stitch : interdit en MVP
            }
            stitch::StitchCommand& cmd = sequence.commands[seq_index];
            if (ov->moved_to) {
                cmd.pos = *ov->moved_to;
                cmd.pass = stitch::StitchPass::Manual;
            }
            if (ov->forced_type) {
                cmd.type = to_command_type(*ov->forced_type);
                cmd.pass = stitch::StitchPass::Manual;
            }
            if (ov->trim_after) {
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

}  // namespace openstitch::stitch_generation
