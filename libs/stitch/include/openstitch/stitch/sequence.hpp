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

// Passe logique de couture (§4) : permet d'afficher, d'analyser et de filtrer
// les sous-couches indépendamment de la couche supérieure. Non sérialisée (la
// séquence est régénérée) ; le DST l'ignore.
enum class StitchPass : std::uint8_t {
    Underlay,   // sous-couche (center/edge/zigzag)
    TopStitch,  // couche supérieure (satin, tatami, contour)
    Travel,     // déplacement caché
    Lock,       // point de fixation
    Manual,     // édité à la main
};

// Position ABSOLUE en micromètres : les deltas sont un détail des codecs
// de format (ADR-003/008). `source` relie chaque commande à l'objet qui l'a
// générée (0 = manuel/importé). `pass` identifie la passe logique.
struct StitchCommand {
    Vec2um pos{};
    CommandType type{CommandType::Stitch};
    ObjectId source{};
    StitchPass pass{StitchPass::TopStitch};

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
