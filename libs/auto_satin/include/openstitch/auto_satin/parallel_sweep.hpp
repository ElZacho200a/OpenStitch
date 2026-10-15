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

// Utilise `spacing`, `min_thread_length`, `sweep_auto_angle` et `sweep_angle_rad` ; les
// autres champs (guides, squelette) n'ont pas d'effet. Les guides ne sont pas appliqués :
// le balayage a son propre angle global et le signale (`orphan_guides` = nombre de
// guides ignorés). Déterministe, sans cache (coût linéaire, pas de raster).
[[nodiscard]] Result<SkeletonSatinResult>
generate_parallel_sweep(const geometry::PathSet& region, const SkeletonSatinParameters& params);

} // namespace openstitch::auto_satin
