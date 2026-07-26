// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "openstitch/auto_satin/skeleton_graph.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::auto_satin {

struct GraphCleanupParameters {
    Micrometers minimum_branch_length{1'500};  // 1,5 mm
    double minimum_length_to_radius_ratio{1.5};
    int maximum_iterations{6};
};

struct RemovedBranch {
    double length_um{0.0};
    double mean_radius_um{0.0};
    std::string reason;
};

struct GraphCleanupResult {
    SkeletonGraph graph;                    // graphe élagué
    std::vector<RemovedBranch> removed;     // branches supprimées (diagnostic)
};

// Élague les branches terminales parasites (§11) : une arête terminale
// (reliée à une extrémité) est supprimée si elle est courte en absolu ET faible
// par rapport au rayon local. Ne supprime jamais tout le graphe. Après élagage,
// les nœuds sont reclassés selon leur degré (les jonctions redevenues de degré 2
// deviennent des continuations). Déterministe. Ne supprime rien silencieusement
// (chaque suppression est diagnostiquée).
[[nodiscard]] GraphCleanupResult prune_graph(const SkeletonGraph& graph,
                                             const GraphCleanupParameters& params);

}  // namespace openstitch::auto_satin
