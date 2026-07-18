// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::stitch {

// Commandes machine (§9 du cahier des charges). Sequin/NeedleChange
// viendront plus tard si nécessaire.
enum class CommandType : std::uint8_t {
    Stitch,       // pénétration d'aiguille à la position donnée
    Jump,         // déplacement sans couture
    Trim,         // coupe du fil (logique — encodage selon le format)
    ColorChange,  // arrêt pour changement de fil
    Stop,         // arrêt machine
    End,          // fin du motif
};

// Position ABSOLUE en micromètres : les deltas sont un détail des codecs
// de format (ADR-003/008). `source` relie chaque commande à l'objet qui l'a
// générée (0 = manuel/importé).
struct StitchCommand {
    Vec2um pos{};
    CommandType type{CommandType::Stitch};
    ObjectId source{};

    bool operator==(const StitchCommand&) const = default;
};

struct StitchSequence {
    std::vector<StitchCommand> commands;

    [[nodiscard]] bool empty() const { return commands.empty(); }
};

struct BoundsUm {
    Vec2um min{};
    Vec2um max{};
};

struct StitchStats {
    std::size_t stitches{0};
    std::size_t jumps{0};
    std::size_t trims{0};
    std::size_t color_changes{0};
    double thread_length_um{0.0};  // somme des longueurs des segments cousus
    BoundsUm bounds{};             // sur les commandes Stitch uniquement
};

[[nodiscard]] StitchStats compute_stats(const StitchSequence& sequence);

}  // namespace openstitch::stitch
