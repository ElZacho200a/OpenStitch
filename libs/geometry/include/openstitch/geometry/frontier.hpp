// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::geometry {

// FRONTIÈRE COMMUNE de deux formes voisines (fondu à la frontière,
// docs/studio-fondu-frontiere-spec.md §5.1).
//
// Portions du bord de `a` (contour extérieur et trous) situées à au plus `tolerance` de `b`,
// en polylignes OUVERTES. La tolérance absorbe les jours et les recouvrements laissés par la
// vectorisation (deux zones « accolées » ne partagent presque jamais un bord exact). Les
// morceaux coupés au point de départ d'un contour sont recollés ; chaque bout d'une frontière
// ouverte est rogné de `tolerance` (amorces le long du contour extérieur) ; les morceaux plus
// courts que `min_length` sont écartés. Ordre déterministe : plus long d'abord, puis premier point
// (x, y). Liste vide si les formes ne se touchent pas. Courbes aplaties à 20 µm.
[[nodiscard]] Result<std::vector<Polyline>>
shared_frontier(const PathSet& a, const PathSet& b, Micrometers tolerance, Micrometers min_length);

// BANDE autour de polylignes ouvertes : ensemble des points à au plus `half_width` d'une
// polyligne (bouts et coudes arrondis), en composantes connexes disjointes. Liste vide si
// `lines` est vide ou si `half_width` <= 0.
[[nodiscard]] Result<std::vector<PathSet>> polyline_band(const std::vector<Polyline>& lines,
                                                         Micrometers half_width);

} // namespace openstitch::geometry
