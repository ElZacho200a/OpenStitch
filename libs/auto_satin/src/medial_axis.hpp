// SPDX-License-Identifier: Apache-2.0
// En-tête interne à libs/auto_satin : axe médian d'une région polygonale par diagramme de
// Voronoï de segments. Aucun type Boost ne traverse cette frontière.
#pragma once

#include <vector>

#include "openstitch/auto_satin/skeleton_graph.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin::detail {

struct MedialAxisGraph {
    // Jonctions (degré >= 3), extrémités (degré 1) et continuations (degré 2), arêtes polylignes
    // avec rayon inscrit : même contrat que le graphe du squelette raster, avant élagage.
    SkeletonGraph graph;

    // Cycles sans aucun noeud (anneau) : polyligne FERMÉE (le dernier point n'est pas répété)
    // et rayon inscrit en chaque point.
    struct Loop {
        std::vector<Vec2um> points;
        std::vector<double> radii;
    };
    std::vector<Loop> loops;
};

// Erreur nommée si un contour se coupe lui-même ou compte trop de segments.
[[nodiscard]] Result<MedialAxisGraph> build_medial_axis_graph(const geometry::PathSet& region);

} // namespace openstitch::auto_satin::detail
