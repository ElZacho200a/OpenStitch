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

// Union de `sets` qui soude aussi les formes séparées par un écart d'au plus `max_gap` (formes
// juxtaposées, ex. deux morceaux issus du Couteau) : union, puis fermeture morphologique
// (décalage extérieur de max_gap/2, union, retrait de max_gap/2) si l'union reste en plusieurs
// morceaux. `max_gap` <= 0 : union simple. Effet de bord : les trous plus étroits que `max_gap`
// sont comblés.
[[nodiscard]] Result<std::vector<PathSet>> union_polygons_closing(const std::vector<PathSet>& sets,
                                                                  Micrometers max_gap);

} // namespace openstitch::geometry
