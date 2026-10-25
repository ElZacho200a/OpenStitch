// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation {

// Remplissage à motif (« motif fill », HP-STI-006) d'une région (contour extérieur + trous,
// polygones déjà aplatis) : le motif de `params.motif` (bibliothèque de `motif_run.hpp`, centré
// sur sa cellule de côté `size`) est répété sur une grille de pas `spacing` tournée de `angle`
// (rangées impaires décalées d'un demi-pas avec `stagger`). Les rangées sont cousues en
// serpentin ; chaque tracé est coupé au contour (règle pair-impair, trous respectés) : seuls les
// points de motif situés dans la région sont cousus, un motif sorti puis rentré donne deux
// tracés. Chaque tracé commence par un point `jump` (le routage `FillLinker` de `generate.cpp`
// le relie par une liaison cachée quand c'est possible). Longueurs de point : au plus
// `stitch_length` (bornée à [1 ; 7] mm), au moins `min_length`. Taille bornée à [0,5 ; 40] mm,
// pas à [1 ; 100] mm. Pure et déterministe.
[[nodiscard]] std::vector<FillStitch> fill_motif(const geometry::PathSet& region,
                                                 const document::MotifFillParams& params);

} // namespace openstitch::stitch_generation
