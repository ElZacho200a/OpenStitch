// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation {

// Couchage d'un tatami (STCH-CROS-01, point de Bayeux) : le tatami est la couche POSÉE ; si
// `couching_spacing` > 0, des fils de couchage la traversent perpendiculairement (rangées
// espacées de `couching_spacing`, borné à [2 ; 20] mm, pénétrations tous les
// `couching_tie_spacing`, borné à [1 ; 7] mm) et chaque pénétration reçoit une attache de
// 1,2 mm en travers du fil de couchage. Vide si `couching_spacing` vaut 0. Le retrait de bord
// est appliqué par l'appelant. Fonction pure et déterministe.
[[nodiscard]] std::vector<FillStitch> couching_pass(const geometry::PathSet& region,
                                                    const document::TatamiParams& params);

} // namespace openstitch::stitch_generation
