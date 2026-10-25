// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation {

// Remplissage RADIAL (HP-STI-008) d'une région (contour extérieur + trous, polygones aplatis) :
// rayons depuis un centre (`params.center`, sinon le milieu de la boîte englobante), cousus dans
// l'ordre angulaire en alternant sortant/rentrant (serpentin, liaisons courtes à la périphérie).
//
// Densité constante à la périphérie : N = 2^m rayons (2π·R/N <= row_spacing, m <= 15), mais un
// rayon n'est cousu qu'à partir du rayon où l'écart avec ses voisins de même niveau reste
// >= row_spacing ; les rayons sont donc plus nombreux vers l'extérieur et les points raccourcissent
// vers le centre. Un rayon qui coupe un trou ou sort d'une région non convexe est cousu par
// tronçons ; une liaison qui sortirait de la région devient un saut (`FillStitch::jump`).
//
// Longueurs de point : au plus `stitch_length` (bornée à [1 ; 7] mm) ; au moins `min_length` sauf
// tronçon plus court qu'elle. Espacement borné à [0,1 ; 10] mm. Le retrait de bord (`inset`) est
// appliqué par l'appelant. Fonction pure et déterministe.
[[nodiscard]] std::vector<FillStitch> fill_radial(const geometry::PathSet& region,
                                                  const document::RadialFillParams& params);

} // namespace openstitch::stitch_generation
