// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Simplification de polyligne par Douglas-Peucker : supprime les nœuds dont
// l'écart au segment de remplacement est inférieur à `tolerance`.
// Un chemin fermé est traité en coupant à ses deux extrêmes ; le résultat
// garde au moins 3 nœuds (fermé) ou 2 nœuds (ouvert).
[[nodiscard]] Path simplify(const Path& path, Micrometers tolerance);

}  // namespace openstitch::geometry
