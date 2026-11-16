// SPDX-License-Identifier: Apache-2.0
#pragma once

// Étape « Vecteurs » du flux Studio : recalcule les formes vectorielles depuis la segmentation de
// l'image active. Partagé par l'espace Vecteurs de Studio et l'opération `vectorize_segmentation`
// (agent, CLI) : même entrée, mêmes formes, octet pour octet.

#include <optional>
#include <utility>
#include <vector>

#include "openstitch/document/project.hpp"

namespace openstitch::project_ops {

struct RevectorizeOptions {
    double precision_mm{0.2};       // tolérance de simplification des contours
    bool curves{false};             // ajuster des courbes de Bézier
    double curve_tolerance_px{1.5}; // tolérance de l'ajustement, en pixels de l'image
    // Calque image à revectoriser ; absent = l'image active (identifiant invalide = l'original).
    std::optional<ObjectId> image_source{};
};

// Plan de mise à jour, à appliquer par des commandes annulables.
struct RevectorizePlan {
    std::vector<std::pair<ObjectId, std::vector<geometry::PathSet>>> replace; // formes changées
    std::vector<document::VectorObject> add; // nouvelles formes, id à réserver à l'exécution
    int keep{0};                             // formes déjà à jour
    int orphan{0}; // formes dont la région n'existe plus (laissées telles quelles)
};

// Plan vide sans segmentation. Ne modifie pas le projet.
[[nodiscard]] RevectorizePlan revectorize(const document::Project& project,
                                          const RevectorizeOptions& options);

} // namespace openstitch::project_ops
