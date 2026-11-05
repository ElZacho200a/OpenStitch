// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- carte de densité (surimpression) : calcule la densité de piqûres de la séquence
// EFFECTIVE (`stitch_generation::effective_sequence`) via `stitch_analysis::compute_density_map`,
// sur un instantané immuable, hors fil d'interface. Le résultat est une image RGBA d'un pixel par
// case, prête à être posée sur l'aperçu, et la liste des zones sur-denses. Sans Qt.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2 {

struct DensityHotspotInfo {
    double centerXmm{0.0}; // repère modèle (Y vers le haut)
    double centerYmm{0.0};
    double maxPenetrationsPerMm2{0.0};
    double areaMm2{0.0};
    std::uint64_t object{0}; // objet ayant le plus de piqûres dans la zone (0 = inconnu)
};

struct DensityOverlay {
    int columns{0};
    int rows{0};
    // RGBA, une case par pixel, ligne 0 = HAUT de l'image (orientation écran).
    std::vector<std::uint8_t> rgba;
    // Rectangle couvert, repère modèle en mm (Y vers le haut), coin bas-gauche + taille.
    double minXmm{0.0};
    double minYmm{0.0};
    double widthMm{0.0};
    double heightMm{0.0};
    double maxPenetrationsPerMm2{0.0};
    double thresholdPerMm2{0.0};
    std::vector<DensityHotspotInfo> hotspots;
    std::uint64_t revision{0}; // révision du document analysée : l'UI écarte un résultat périmé

    [[nodiscard]] bool empty() const { return rgba.empty(); }
};

// Couleur (RGBA) d'une case de densité `perMm2` pour un seuil `threshold` : transparente tant que
// la case reste sous le quart du seuil, jaune puis orange en s'en approchant, rouge au-delà.
[[nodiscard]] std::array<std::uint8_t, 4> densityColour(double perMm2, double threshold);

[[nodiscard]] Result<DensityOverlay> run_density(std::shared_ptr<const document::Project> project,
                                                 std::uint64_t revision, const TaskContext& task);

} // namespace openstitch::desktop_v2
