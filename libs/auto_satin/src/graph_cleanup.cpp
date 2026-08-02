// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/graph_cleanup.hpp"

#include <algorithm>
#include <numeric>

namespace openstitch::auto_satin {

namespace {

double mean_radius(const SkeletonEdge& e) {
    if (e.local_radii_um.empty()) {
        return 0.0;
    }
    double s = 0.0;
    for (const double r : e.local_radii_um) {
        s += r;
    }
    return s / static_cast<double>(e.local_radii_um.size());
}

// Degré d'un nœud = nombre d'arêtes vivantes incidentes.
std::vector<int> degrees(const SkeletonGraph& g, const std::vector<char>& alive) {
    std::vector<int> deg(g.nodes.size(), 0);
    for (std::size_t i = 0; i < g.edges.size(); ++i) {
        if (!alive[i]) {
            continue;
        }
        deg[g.edges[i].from]++;
        deg[g.edges[i].to]++;
    }
    return deg;
}

}  // namespace

GraphCleanupResult prune_graph(const SkeletonGraph& graph, const GraphCleanupParameters& params) {
    GraphCleanupResult result;
    std::vector<char> alive(graph.edges.size(), 1);
    const double minLen = static_cast<double>(params.minimum_branch_length.value);

    int aliveCount = static_cast<int>(graph.edges.size());
    for (int iter = 0; iter < params.maximum_iterations && aliveCount > 1; ++iter) {
        const std::vector<int> deg = degrees(graph, alive);
        bool changed = false;
        for (std::size_t i = 0; i < graph.edges.size(); ++i) {
            if (!alive[i]) {
                continue;
            }
            const SkeletonEdge& e = graph.edges[i];
            const bool terminal = deg[e.from] == 1 || deg[e.to] == 1;
            if (!terminal) {
                continue;
            }
            const double r = mean_radius(e);
            const double threshold = std::max(minLen, params.minimum_length_to_radius_ratio * r);
            if (e.length_um < threshold && aliveCount > 1) {
                alive[i] = 0;
                --aliveCount;
                changed = true;
                result.removed.push_back(
                    {e.length_um, r,
                     "branche terminale courte (longueur " +
                         std::to_string(static_cast<int>(e.length_um / 100) / 10.0) +
                         " mm < seuil)"});
            }
        }
        if (!changed) {
            break;
        }
    }

    // Reconstruit le graphe avec les arêtes survivantes et les nœuds référencés,
    // reclassés par degré. Ordre stable (par identifiant d'origine).
    const std::vector<int> finalDeg = degrees(graph, alive);
    std::vector<std::uint32_t> remap(graph.nodes.size(), 0xFFFFFFFF);
    for (std::size_t i = 0; i < graph.edges.size(); ++i) {
        if (!alive[i]) {
            continue;
        }
        for (const std::uint32_t nid : {graph.edges[i].from, graph.edges[i].to}) {
            if (remap[nid] == 0xFFFFFFFF) {
                remap[nid] = static_cast<std::uint32_t>(result.graph.nodes.size());
                SkeletonNode n = graph.nodes[nid];
                n.id = remap[nid];
                const int d = finalDeg[nid];
                n.type = d == 1     ? SkeletonNodeType::Endpoint
                         : d >= 3   ? SkeletonNodeType::Junction
                         : d == 2   ? SkeletonNodeType::Continuation
                                    : SkeletonNodeType::Isolated;
                result.graph.nodes.push_back(n);
            }
        }
    }
    for (std::size_t i = 0; i < graph.edges.size(); ++i) {
        if (!alive[i]) {
            continue;
        }
        SkeletonEdge e = graph.edges[i];
        e.id = static_cast<std::uint32_t>(result.graph.edges.size());
        e.from = remap[e.from];
        e.to = remap[e.to];
        result.graph.edges.push_back(std::move(e));
    }

    // Un nœud du graphe d'origine sans AUCUNE arête vivante incidente (isolé dès
    // le départ, ou devenu orphelin après élagage d'une arête terminale courte)
    // n'est référencé par aucune paire (from, to) ci-dessus et disparaîtrait donc
    // du graphe résultat sans jamais apparaître dans `remap` ni dans `removed` —
    // violation du contrat documenté (« ne supprime rien silencieusement »).
    // Défaut trouvé par revue. Diagnostiqué ici explicitement.
    for (std::size_t nid = 0; nid < graph.nodes.size(); ++nid) {
        if (remap[nid] != 0xFFFFFFFF) {
            continue;
        }
        result.removed.push_back({0.0, graph.nodes[nid].local_radius_um,
                                   "noeud sans arete vivante (isole ou orphelin apres elagage)"});
    }
    return result;
}

}  // namespace openstitch::auto_satin
