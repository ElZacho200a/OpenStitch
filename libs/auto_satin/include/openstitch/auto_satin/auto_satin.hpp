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
    SkeletonGraph raw_graph;                 // avant élagage
    SkeletonGraph graph;                     // après élagage (utilisé pour le rapport)
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

}  // namespace openstitch::auto_satin
