// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Nettoie des contours fermés bruts (auto-intersections, orientations
// incohérentes, micro-arêtes) par une union booléenne en coordonnées
// entières (Clipper2, règle pair-impair), et reconstruit la hiérarchie
// extérieur/trous. Étape systématique avant toute génération de points.
// Peut renvoyer plusieurs PathSet si la géométrie se sépare en morceaux,
// ou aucun si tout est dégénéré.
[[nodiscard]] Result<std::vector<PathSet>> clean_to_path_sets(const std::vector<Path>& raw);

}  // namespace openstitch::geometry
