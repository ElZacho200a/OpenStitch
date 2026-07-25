// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Un point de remplissage : sa position et s'il est atteint par un DÉPLACEMENT
// (aiguille relevée) ou par un point COUSU. Le routage (§15) garantit qu'aucun
// point cousu ne traverse un trou ni ne sort de la région : les liaisons
// invalides deviennent des déplacements.
struct FillStitch {
    Vec2um pos{};
    bool travel{false};  // true = déplacement vers ce point ; false = point cousu

    bool operator==(const FillStitch&) const = default;
};

// Remplissage tatami d'une région (contour extérieur + trous), par balayage
// de lignes parallèles (§5.4) :
// - rangées espacées de `row_spacing`, orientées selon `angle` ;
// - intersection des lignes avec le polygone (règle pair-impair, trous gérés) ;
// - couture en serpentin (alternance du sens d'une rangée à l'autre) ;
// - subdivision de chaque rangée en points <= `stitch_length` ;
// - décalage régulier des pénétrations d'aiguille selon `stagger`.
// Une liaison entre deux segments (même rangée, ou d'une rangée à l'autre) qui
// sortirait de la région ou traverserait un trou est classée en DÉPLACEMENT,
// jamais en point cousu (corrige le débordement hors région).
// Le retrait de bord (`inset`) est appliqué par l'appelant en amont.
[[nodiscard]] std::vector<FillStitch> fill_tatami(const geometry::PathSet& region,
                                                  const document::TatamiParams& params);

}  // namespace openstitch::stitch_generation
