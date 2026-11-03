// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation {

// Remplissage CROISÉ / crosshatch (HP-STI-014) d'une région : `layers` (2 ou 3, borné) tatamis
// très espacés (`row_spacing`, borné à [0,3 ; 10] mm) dont les angles se répartissent sur 180°
// à partir de `angle` (2 couches : perpendiculaires ; 3 : à 60°). Chaque couche est un
// `fill_tatami` complet ; la première pénétration de chaque couche suivante est un saut
// (`FillStitch::jump`), que la génération relie par un trajet caché si possible. Le retrait de
// bord (`inset`) est appliqué par l'appelant. Fonction pure et déterministe.
[[nodiscard]] std::vector<FillStitch> fill_crosshatch(const geometry::PathSet& region,
                                                      const document::CrosshatchFillParams& params);

} // namespace openstitch::stitch_generation
