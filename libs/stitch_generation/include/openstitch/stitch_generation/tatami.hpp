// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Remplissage tatami d'une région (contour extérieur + trous), par balayage
// de lignes parallèles (§5.4) :
// - rangées espacées de `row_spacing`, orientées selon `angle` ;
// - intersection des lignes avec le polygone (règle pair-impair, trous gérés) ;
// - couture en serpentin (alternance du sens d'une rangée à l'autre) ;
// - subdivision de chaque rangée en points <= `stitch_length` ;
// - décalage régulier des pénétrations d'aiguille selon `stagger` (les points
//   ne s'alignent pas d'une rangée à l'autre : moins de sillon visible).
// Le retrait de bord (`inset`) est appliqué par l'appelant en amont.
// Renvoie une polyligne continue (les liaisons entre rangées sont incluses).
[[nodiscard]] std::vector<Vec2um> fill_tatami(const geometry::PathSet& region,
                                              const document::TatamiParams& params);

}  // namespace openstitch::stitch_generation
