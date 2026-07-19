// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Décale les contours d'un PathSet vers l'intérieur (delta > 0) ou vers
// l'extérieur (delta < 0), via Clipper2 (jointures en biseau). Utilisé pour
// la compensation de contour du remplissage et les sous-couches.
// Un retrait qui fait disparaître la forme renvoie une liste vide.
[[nodiscard]] Result<std::vector<PathSet>> inset_path_set(const PathSet& set, Micrometers delta);

}  // namespace openstitch::geometry
