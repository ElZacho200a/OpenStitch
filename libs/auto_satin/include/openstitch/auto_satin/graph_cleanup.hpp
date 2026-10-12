// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "openstitch/auto_satin/skeleton_graph.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::auto_satin {

struct GraphCleanupParameters {
    Micrometers minimum_branch_length{1'500}; // 1,5 mm
    double minimum_length_to_radius_ratio{1.5};
    int maximum_iterations{6};

    // Élagage par ERREUR BORNÉE (squelette stable, bruit de contour). Une
    // branche terminale qui part d'une jonction de rayon `r_J` et finit par une
    // extrémité de rayon `r_E`, de longueur `L`, ne dépasse du disque de la
    // jonction que de `h = L + r_E - r_J` : c'est la borne (de Hausdorff) de
    // l'écart à la forme reconstruite si on la supprime, car `s + r(s)` ne décroît
    // jamais le long d'une branche (le rayon varie au plus d'autant que la
    // distance). On supprime la branche si `h <= max(max_protrusion,
    // max_protrusion_to_radius * r_J)`. Les branches qui portent un vrai détail
    // (h grand) restent quelle que soit leur longueur. 0 et 0 : désactivé
    // (comportement historique inchangé).
    Micrometers max_protrusion{0};
    double max_protrusion_to_radius{0.0};
};

struct RemovedBranch {
    double length_um{0.0};
    double mean_radius_um{0.0};
    std::string reason;
};

struct GraphCleanupResult {
    SkeletonGraph graph;                // graphe élagué
    std::vector<RemovedBranch> removed; // branches supprimées (diagnostic)
};

// Élague les branches terminales parasites (§11) : une arête terminale
// (reliée à une extrémité) est supprimée si elle est courte en absolu ET faible
// par rapport au rayon local. Ne supprime jamais tout le graphe. Après élagage,
// les nœuds sont reclassés selon leur degré (les jonctions redevenues de degré 2
// deviennent des continuations). Déterministe. Ne supprime rien silencieusement
// (chaque suppression est diagnostiquée).
[[nodiscard]] GraphCleanupResult prune_graph(const SkeletonGraph& graph,
                                             const GraphCleanupParameters& params);

} // namespace openstitch::auto_satin
