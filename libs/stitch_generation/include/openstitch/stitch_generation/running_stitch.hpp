// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Échantillonne une polyligne en pas réguliers (§5.1) :
// - chaque arête est subdivisée en pas égaux <= stitch_length (interpolation
//   régulière, subdivision des segments trop longs) ;
// - les nœuds (coins) sont TOUJOURS conservés : la forme n'est pas déformée ;
// - les points intermédiaires générés à moins de min_length du point
//   précédent sont fusionnés.
// Un chemin fermé revient à son point de départ (dernier point = premier).
[[nodiscard]] std::vector<Vec2um> sample_path(const geometry::Path& path,
                                              Micrometers stitch_length, Micrometers min_length);

// Répétition (§5.1) : 1 = point simple ; 2 = aller-retour complet (termine
// au point de départ) ; 3 = point triple (chaque segment cousu 3 fois :
// avant, arrière, avant).
[[nodiscard]] std::vector<Vec2um> apply_repeats(const std::vector<Vec2um>& points, int repeats);

}  // namespace openstitch::stitch_generation
