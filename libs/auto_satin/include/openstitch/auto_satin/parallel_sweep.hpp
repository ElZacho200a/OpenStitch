// SPDX-License-Identifier: Apache-2.0
// Auto-satin par balayage parallèle (docs/auto-satin-research.md, famille « balayage
// parallèle », lot D). Indépendant du squelette : des droites à angle constant, espacées
// de `spacing`, sont intersectées avec la région ; les intervalles intérieurs de droites
// consécutives qui se recouvrent un à un forment une bande continue (une colonne).
// Une concavité, un trou ou une branche sépare les intervalles : à chaque division ou
// fusion les bandes concernées s'arrêtent et de nouvelles commencent, de sorte que le
// fil ne traverse jamais le vide entre deux intervalles.
#pragma once

#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

// Utilise `spacing`, `min_thread_length`, `sweep_auto_angle`, `sweep_angle_rad` et `guides` ;
// les champs du squelette n'ont pas d'effet. Sans guide : droites parallèles à l'angle global.
// Avec guides : chaque guide fixe l'angle des fils à sa position le long de l'axe de balayage,
// l'angle est interpolé linéairement entre guides (constant au-delà), l'écart à l'angle global
// est borné à ±60° et son taux de variation limité pour que deux droites voisines ne se
// coupent pas dans la forme (`clamped_angle` compte les droites touchées). Un guide hors de la
// forme est compté dans `orphan_guides`. Déterministe, sans cache (coût linéaire, pas de raster).
[[nodiscard]] Result<SkeletonSatinResult>
generate_parallel_sweep(const geometry::PathSet& region, const SkeletonSatinParameters& params);

} // namespace openstitch::auto_satin
