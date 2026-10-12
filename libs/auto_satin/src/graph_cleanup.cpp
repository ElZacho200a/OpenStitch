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

} // namespace

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

    // Élagage par erreur bornée (voir GraphCleanupParameters::max_protrusion).
    const double protrusionAbs = static_cast<double>(params.max_protrusion.value);
    if (protrusionAbs > 0.0 || params.max_protrusion_to_radius > 0.0) {
        for (int iter = 0; iter < params.maximum_iterations && aliveCount > 1; ++iter) {
            const std::vector<int> deg = degrees(graph, alive);
            struct Candidate {
                double h;
                std::size_t edge;
                double junctionRadius;
                double radiusEnd;
            };
            std::vector<Candidate> candidates;
            for (std::size_t i = 0; i < graph.edges.size(); ++i) {
                if (!alive[i]) {
                    continue;
                }
                const SkeletonEdge& e = graph.edges[i];
                // Terminale : une extrémité de degré 1, l'autre une vraie jonction.
                std::uint32_t end;
                std::uint32_t junction;
                if (deg[e.from] == 1 && deg[e.to] >= 3) {
                    end = e.from;
                    junction = e.to;
                } else if (deg[e.to] == 1 && deg[e.from] >= 3) {
                    end = e.to;
                    junction = e.from;
                } else {
                    continue;
                }
                const double rJ = graph.nodes[junction].local_radius_um;
                const double rE = graph.nodes[end].local_radius_um;
                const double h = e.length_um + rE - rJ;
                const double threshold =
                    std::max(protrusionAbs, params.max_protrusion_to_radius * rJ);
                if (h <= threshold) {
                    candidates.push_back({h, i, rJ, rE});
                }
            }
            if (candidates.empty()) {
                break;
            }
            std::sort(candidates.begin(), candidates.end(),
                      [](const Candidate& a, const Candidate& b) {
                          return a.h != b.h ? a.h < b.h : a.edge < b.edge;
                      });
            bool changed = false;
            for (const Candidate& c : candidates) {
                if (aliveCount <= 1) {
                    break;
                }
                // Un nœud ne perd jamais plus de branches qu'il n'en garde : on ne
                // retire pas tous les rameaux d'une jonction de degré 3 d'un coup
                // si cela ne laisse qu'un tronçon (le degré est recalculé au tour suivant).
                alive[c.edge] = 0;
                --aliveCount;
                changed = true;
                result.removed.push_back(
                    {graph.edges[c.edge].length_um, c.radiusEnd,
                     "branche terminale non significative (saillie " +
                         std::to_string(static_cast<int>(std::max(0.0, c.h) / 100) / 10.0) +
                         " mm sous le seuil)"});
            }
            if (!changed) {
                break;
            }
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
                n.type = d == 1   ? SkeletonNodeType::Endpoint
                         : d >= 3 ? SkeletonNodeType::Junction
                         : d == 2 ? SkeletonNodeType::Continuation
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

} // namespace openstitch::auto_satin
