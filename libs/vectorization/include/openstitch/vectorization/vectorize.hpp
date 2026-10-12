// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::vectorization {

struct VectorizeOptions {
    Millimeters mm_per_px{25.4 / 96.0};  // résolution de travail de l'image
    Micrometers simplify_tolerance{200}; // 0,2 mm : sous le pas machine (0,1 mm) x2
    // > 0 : les contours deviennent des COURBES de Bézier (ajustement de
    // Schneider avec détection des coins, `geometry::fit_curves`) au lieu de
    // polygones Douglas-Peucker ; `simplify_tolerance` est alors ignorée. L'écart
    // maximal au contour de pixels est `curve_tolerance` (bornée à périmètre/16
    // pour les petits trous). Prendre ~1,5 px ou plus : en dessous, l'escalier de
    // la trame empêche de réduire le nombre de nœuds. 0 (défaut) : polygones.
    Micrometers curve_tolerance{0};
};

// Transforme une région segmentée en contours vectoriels propres :
// extraction des contours et des trous (Suzuki-Abe via OpenCV), conversion
// en coordonnées physiques (µm, origine au centre de l'image, Y vers le
// haut), simplification Douglas-Peucker puis nettoyage Clipper2.
// Une région en plusieurs morceaux produit plusieurs PathSet.
[[nodiscard]] Result<std::vector<geometry::PathSet>>
vectorize_region(const segmentation::Segmentation& seg, RegionId id,
                 const VectorizeOptions& options);

} // namespace openstitch::vectorization
