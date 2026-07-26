// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/auto_satin.hpp"

#include "openstitch/auto_satin/skeleton.hpp"

namespace openstitch::auto_satin {

Result<AutoSatinAnalysis> analyze_region(const geometry::PathSet& region,
                                         const AutoSatinParameters& params) {
    auto mask = rasterize(region, params.raster);
    if (!mask) {
        return std::unexpected(mask.error());
    }

    AutoSatinAnalysis out;
    out.debug.mask = *mask;
    out.debug.distance = distance_transform(*mask);
    out.debug.skeleton = thin_zhang_suen(*mask);
    out.debug.raw_graph = build_skeleton_graph(out.debug.skeleton, out.debug.distance);
    // Élagage des branches parasites (§11) avant analyse.
    auto cleaned = prune_graph(out.debug.raw_graph, params.cleanup);
    out.debug.graph = std::move(cleaned.graph);
    out.debug.removed_branches = std::move(cleaned.removed);
    out.report =
        evaluate_satinability(region, out.debug.graph, out.debug.distance, params.thresholds);
    return out;
}

}  // namespace openstitch::auto_satin
