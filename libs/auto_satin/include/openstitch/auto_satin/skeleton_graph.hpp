// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

#include "openstitch/auto_satin/distance_field.hpp"
#include "openstitch/auto_satin/raster.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::auto_satin {

enum class SkeletonNodeType { Endpoint, Junction, Isolated, Continuation };

struct SkeletonNode {
    std::uint32_t id{0};
    Vec2um position{};
    SkeletonNodeType type{SkeletonNodeType::Endpoint};
    double local_radius_um{0.0};
};

struct SkeletonEdge {
    std::uint32_t id{0};
    std::uint32_t from{0};
    std::uint32_t to{0};
    std::vector<Vec2um> centerline;      // du nœud `from` au nœud `to`
    std::vector<double> local_radii_um;  // rayon local le long de la centerline
    double length_um{0.0};
};

// Graphe explicite du squelette : les chaînes de pixels de degré 2 sont
// compressées en arêtes polylignes. Déterministe (nœuds triés par (row, col),
// arêtes par (from, to, premier point)).
struct SkeletonGraph {
    std::vector<SkeletonNode> nodes;
    std::vector<SkeletonEdge> edges;

    [[nodiscard]] std::size_t endpoint_count() const;
    [[nodiscard]] std::size_t junction_count() const;
};

// Construit le graphe à partir d'un masque de squelette (1 px de large) et du
// champ de distance (pour le rayon local).
[[nodiscard]] SkeletonGraph build_skeleton_graph(const RasterMask& skeleton,
                                                 const DistanceField& distance);

}  // namespace openstitch::auto_satin
