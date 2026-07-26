// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Un point de remplissage : sa position et s'il est atteint par un SAUT
// (aiguille levée, sans couture — un « jump » machine) ou par un point COUSU.
// Chaque trajet cousu est validé géométriquement contre la région et ses trous ;
// à défaut de trajet intérieur valide, la liaison est un saut.
//
// Terminologie : ce drapeau distingue Stitch (cousu) de Jump (aiguille levée).
// Le vrai « travel stitch » de broderie — un déplacement COUSU caché sous la
// couche supérieure (underpath) — n'est PAS implémenté : les liaisons non
// cousables sont donc des sauts, pas des sous-chemins cachés.
struct FillStitch {
    Vec2um pos{};
    bool jump{false};  // true = saut (aiguille levée) ; false = point cousu

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
