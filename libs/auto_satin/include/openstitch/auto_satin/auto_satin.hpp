// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/auto_satin/distance_field.hpp"
#include "openstitch/auto_satin/graph_cleanup.hpp"
#include "openstitch/auto_satin/raster.hpp"
#include "openstitch/auto_satin/satinability.hpp"
#include "openstitch/auto_satin/skeleton_graph.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

// Données de diagnostic de chaque étape (désactivables en production).
struct AutoSatinDebug {
    RasterMask mask;
    DistanceField distance;
    RasterMask skeleton;
    SkeletonGraph raw_graph; // avant élagage
    SkeletonGraph graph;     // après élagage (utilisé pour le rapport)
    std::vector<RemovedBranch> removed_branches;
};

struct AutoSatinParameters {
    SkeletonRasterParameters raster{};
    GraphCleanupParameters cleanup{};
    SatinabilityThresholds thresholds{};
};

struct AutoSatinAnalysis {
    SatinabilityReport report;
    AutoSatinDebug debug;
};

// Mission 1 : rasterisation → transformée de distance → squelette (Zhang-Suen)
// → graphe → rapport de satinabilité. NE génère PAS encore les rails ni les
// points satin. Déterministe.
[[nodiscard]] Result<AutoSatinAnalysis> analyze_region(const geometry::PathSet& region,
                                                       const AutoSatinParameters& params);

namespace detail {
struct SkeletonCache;
} // namespace detail

// Mémoïsation de l'étape squelette d'`analyze_region` (rasterisation,
// transformée de distance, Zhang-Suen, graphe brut -- ~95 % de son coût)
// pendant la vie de l'objet, sur le thread courant uniquement. La
// planification satin analyse plusieurs fois la même géométrie (région
// entière, puis colonnes, puis décomposition, puis morceaux replanifiés) :
// 57 % des appels étaient des doublons exacts sur la fixture tentabrode
// (docs/performance-audit.md).
//
// - Clé = contenu exact (positions de tous les nœuds, extérieur et trous,
//   dans l'ordre, et paramètres de rasterisation), comparé intégralement :
//   un résultat servi depuis le cache est identique à un recalcul.
// - Invalidation : aucune donnée du document n'y entre ; tout est libéré à
//   la destruction du scope le plus externe (fin de la planification d'une
//   région), jamais conservé entre deux appels de haut niveau.
// - Scopes imbriqués : seul le plus externe possède le cache.
// - Charge utile bornée à 64 Mio (clés, rasters, champs de distance et
//   graphes inclus ; éviction FIFO des plus anciennes entrées).
class SkeletonCacheScope {
public:
    SkeletonCacheScope();
    ~SkeletonCacheScope();
    SkeletonCacheScope(const SkeletonCacheScope&) = delete;
    SkeletonCacheScope& operator=(const SkeletonCacheScope&) = delete;

private:
    detail::SkeletonCache* owned_{nullptr};
};

} // namespace openstitch::auto_satin
