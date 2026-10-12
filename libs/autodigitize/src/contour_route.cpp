// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/contour_route.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace openstitch::autodigitize {

namespace {

constexpr int kVirtual = -1;

struct Edge {
    std::int32_t u{0};
    std::int32_t v{0};
    int seg{kVirtual}; // indice de segment, ou kVirtual (liaison par saut)
    bool duplicate{false};
};

double dist_um(Vec2um a, Vec2um b) {
    return length_um(a - b);
}

// Direction (non normalisée) en quittant le nœud par le côté `atStart` du
// segment, mesurée sur ~1 mm.
Vec2um leave_direction(const ContourSegment& s, bool atStart) {
    std::vector<Vec2um> p = s.centerline;
    if (!atStart) {
        std::reverse(p.begin(), p.end());
    }
    if (p.size() < 2) {
        return Vec2um{};
    }
    double acc = 0.0;
    for (std::size_t i = 1; i < p.size(); ++i) {
        acc += length_um(p[i] - p[i - 1]);
        if (acc >= 1000.0 || i + 1 == p.size()) {
            return p[i] - p[0];
        }
    }
    return Vec2um{};
}

double cos_between(Vec2um a, Vec2um b) {
    const double la = length_um(a);
    const double lb = length_um(b);
    if (la <= 0.0 || lb <= 0.0) {
        return 0.0;
    }
    return (static_cast<double>(a.x.value) * b.x.value +
            static_cast<double>(a.y.value) * b.y.value) /
           (la * lb);
}

} // namespace

std::vector<RoutedTrail> route_single_path(const ContourComponent& comp,
                                           const std::vector<std::size_t>& segs,
                                           const ContourRouteParams& params, RouteStats* stats) {
    std::vector<RoutedTrail> out;
    RouteStats st;

    // --- Graphe : une arête par segment ouvert relié à deux nœuds -------------
    std::vector<Edge> edges;
    std::vector<std::size_t> standalone; // à rendre tels quels
    for (const std::size_t si : segs) {
        const ContourSegment& s = comp.segments[si];
        if (s.closed || s.start_node < 0 || s.end_node < 0 || s.centerline.size() < 2) {
            standalone.push_back(si);
            continue;
        }
        edges.push_back({s.start_node, s.end_node, static_cast<int>(si), false});
    }
    const std::size_t realEdgeCount = edges.size();
    const auto nodeCount = static_cast<std::int32_t>(comp.nodes.size());

    const auto degreeOf = [&](std::int32_t n) {
        std::size_t d = 0;
        for (const Edge& e : edges) {
            d += (e.u == n ? 1U : 0U) + (e.v == n ? 1U : 0U);
        }
        return d;
    };

    // Composantes connexes du graphe (par plus petit indice de nœud).
    std::vector<std::int32_t> comp_of(static_cast<std::size_t>(nodeCount), -1);
    std::int32_t compCount = 0;
    {
        std::vector<std::vector<std::int32_t>> adj(static_cast<std::size_t>(nodeCount));
        for (const Edge& e : edges) {
            adj[static_cast<std::size_t>(e.u)].push_back(e.v);
            adj[static_cast<std::size_t>(e.v)].push_back(e.u);
        }
        for (std::int32_t n = 0; n < nodeCount; ++n) {
            if (comp_of[static_cast<std::size_t>(n)] >= 0 ||
                adj[static_cast<std::size_t>(n)].empty()) {
                continue;
            }
            std::vector<std::int32_t> stack{n};
            comp_of[static_cast<std::size_t>(n)] = compCount;
            while (!stack.empty()) {
                const std::int32_t c = stack.back();
                stack.pop_back();
                for (const std::int32_t d : adj[static_cast<std::size_t>(c)]) {
                    if (comp_of[static_cast<std::size_t>(d)] < 0) {
                        comp_of[static_cast<std::size_t>(d)] = compCount;
                        stack.push_back(d);
                    }
                }
            }
            ++compCount;
        }
    }

    const auto nodePos = [&](std::int32_t n) {
        return comp.nodes[static_cast<std::size_t>(n)].position;
    };

    for (std::int32_t cc = 0; cc < compCount; ++cc) {
        // Arêtes et nœuds de cette composante.
        std::vector<std::size_t> myEdges;
        for (std::size_t i = 0; i < realEdgeCount; ++i) {
            if (comp_of[static_cast<std::size_t>(edges[i].u)] == cc) {
                myEdges.push_back(i);
            }
        }
        std::set<std::int32_t> myNodes;
        for (const std::size_t i : myEdges) {
            myNodes.insert(edges[i].u);
            myNodes.insert(edges[i].v);
        }

        // --- Doublage : relier des impairs lointains par un trajet repassé -----
        const auto oddNodes = [&] {
            std::vector<std::int32_t> odd;
            for (const std::int32_t n : myNodes) {
                if (degreeOf(n) % 2 == 1) {
                    odd.push_back(n);
                }
            }
            return odd;
        };
        st.odd_nodes += oddNodes().size();

        for (;;) {
            const std::vector<std::int32_t> odd = oddNodes();
            if (odd.size() < 2) {
                break;
            }
            // Dijkstra depuis chaque impair sur les longueurs de segments
            // (toutes arêtes présentes, doublons compris).
            double bestCost = std::numeric_limits<double>::max();
            std::vector<std::size_t> bestPath;
            std::int32_t bestA = -1;
            for (const std::int32_t src : odd) {
                std::map<std::int32_t, double> dist;
                std::map<std::int32_t, std::size_t> via; // arête d'arrivée
                std::set<std::pair<double, std::int32_t>> queue;
                dist[src] = 0.0;
                queue.insert({0.0, src});
                while (!queue.empty()) {
                    const auto [d, n] = *queue.begin();
                    queue.erase(queue.begin());
                    if (d > static_cast<double>(params.max_retrace.value)) {
                        break;
                    }
                    for (const std::size_t ei : myEdges) {
                        const Edge& e = edges[ei];
                        std::int32_t other;
                        if (e.u == n) {
                            other = e.v;
                        } else if (e.v == n) {
                            other = e.u;
                        } else {
                            continue;
                        }
                        const double nd =
                            d + comp.segments[static_cast<std::size_t>(e.seg)].length_um;
                        const auto it = dist.find(other);
                        if (it == dist.end() || nd < it->second - 1e-9) {
                            if (it != dist.end()) {
                                queue.erase({it->second, other});
                            }
                            dist[other] = nd;
                            via[other] = ei;
                            queue.insert({nd, other});
                        }
                    }
                }
                for (const std::int32_t dst : odd) {
                    if (dst <= src) {
                        continue;
                    }
                    const auto it = dist.find(dst);
                    if (it == dist.end() ||
                        it->second > static_cast<double>(params.max_retrace.value)) {
                        continue;
                    }
                    // Un simple saut suffit si les extrémités sont proches.
                    if (dist_um(nodePos(src), nodePos(dst)) <
                        static_cast<double>(params.trim_threshold.value)) {
                        continue;
                    }
                    if (it->second < bestCost - 1e-9) {
                        bestCost = it->second;
                        bestA = src;
                        // Reconstitution du chemin dst -> src.
                        bestPath.clear();
                        std::int32_t cur = dst;
                        while (cur != src) {
                            const std::size_t ei = via.at(cur);
                            bestPath.push_back(ei);
                            cur = edges[ei].u == cur ? edges[ei].v : edges[ei].u;
                        }
                    }
                }
            }
            if (bestA < 0) {
                break;
            }
            for (const std::size_t ei : bestPath) {
                Edge copy = edges[ei];
                copy.duplicate = true;
                edges.push_back(copy);
                myEdges.push_back(edges.size() - 1);
                st.retraced_um += comp.segments[static_cast<std::size_t>(copy.seg)].length_um;
            }
            ++st.retraces;
        }

        // --- Liaisons virtuelles : les impairs restants, au plus proche -------
        std::vector<std::int32_t> odd = oddNodes();
        while (odd.size() >= 2) {
            double best = std::numeric_limits<double>::max();
            std::size_t bi = 0;
            std::size_t bj = 1;
            for (std::size_t i = 0; i < odd.size(); ++i) {
                for (std::size_t j = i + 1; j < odd.size(); ++j) {
                    const double d = dist_um(nodePos(odd[i]), nodePos(odd[j]));
                    if (d < best - 1e-9) {
                        best = d;
                        bi = i;
                        bj = j;
                    }
                }
            }
            edges.push_back({odd[bi], odd[bj], kVirtual, false});
            myEdges.push_back(edges.size() - 1);
            odd.erase(odd.begin() + static_cast<std::ptrdiff_t>(bj));
            odd.erase(odd.begin() + static_cast<std::ptrdiff_t>(bi));
        }

        // --- Circuit eulérien (Hierholzer), départ sur une liaison virtuelle ---
        std::map<std::int32_t, std::vector<std::size_t>> adj;
        for (const std::size_t ei : myEdges) {
            adj[edges[ei].u].push_back(ei);
            if (edges[ei].v != edges[ei].u) {
                adj[edges[ei].v].push_back(ei);
            } else {
                adj[edges[ei].u].push_back(ei);
            }
        }
        std::int32_t start = *myNodes.begin();
        for (const std::size_t ei : myEdges) {
            if (edges[ei].seg == kVirtual) {
                start = std::min(edges[ei].u, edges[ei].v);
                break;
            }
        }
        struct Step {
            std::int32_t node;
            std::size_t edge; // arête d'arrivée (SIZE_MAX au départ)
            bool forward;     // arrivée de u vers v
        };
        std::vector<bool> used(edges.size(), false);
        std::vector<Step> stack{{start, std::numeric_limits<std::size_t>::max(), true}};
        std::vector<Step> circuit;
        while (!stack.empty()) {
            const Step top = stack.back();
            // Arête non utilisée la plus alignée avec l'arrivée.
            std::size_t pick = std::numeric_limits<std::size_t>::max();
            double bestDot = -2.0;
            Vec2um incoming{};
            bool haveIncoming = false;
            if (top.edge != std::numeric_limits<std::size_t>::max() &&
                edges[top.edge].seg != kVirtual) {
                const ContourSegment& s =
                    comp.segments[static_cast<std::size_t>(edges[top.edge].seg)];
                // direction d'arrivée = opposée à la sortie par le bout d'arrivée
                const Vec2um out = leave_direction(s, !top.forward);
                incoming = Vec2um{Micrometers{-out.x.value}, Micrometers{-out.y.value}};
                haveIncoming = true;
            }
            for (const std::size_t ei : adj[top.node]) {
                if (used[ei]) {
                    continue;
                }
                double score = 0.0;
                if (haveIncoming && edges[ei].seg != kVirtual) {
                    const bool atStart = edges[ei].u == top.node;
                    score = cos_between(
                        incoming,
                        leave_direction(comp.segments[static_cast<std::size_t>(edges[ei].seg)],
                                        atStart));
                }
                if (edges[ei].seg == kVirtual) {
                    score = -1.5; // une liaison par saut passe en dernier recours
                }
                if (score > bestDot + 1e-12) {
                    bestDot = score;
                    pick = ei;
                }
            }
            if (pick != std::numeric_limits<std::size_t>::max()) {
                used[pick] = true;
                const bool fwd = edges[pick].u == top.node;
                stack.push_back({fwd ? edges[pick].v : edges[pick].u, pick, fwd});
            } else {
                stack.pop_back();
                if (top.edge != std::numeric_limits<std::size_t>::max()) {
                    circuit.push_back(top);
                }
            }
        }
        std::reverse(circuit.begin(), circuit.end());

        // --- Découpe aux liaisons virtuelles -----------------------------------
        std::size_t firstVirtual = 0;
        bool anyVirtual = false;
        for (std::size_t i = 0; i < circuit.size(); ++i) {
            if (edges[circuit[i].edge].seg == kVirtual) {
                firstVirtual = i;
                anyVirtual = true;
                break;
            }
        }
        std::vector<Step> rotated;
        if (anyVirtual) {
            for (std::size_t i = 1; i <= circuit.size(); ++i) {
                rotated.push_back(circuit[(firstVirtual + i) % circuit.size()]);
            }
        } else {
            rotated = circuit;
        }
        RoutedTrail cur;
        const auto flush = [&](bool closedCircuit) {
            if (cur.points.size() >= 2) {
                cur.closed = closedCircuit;
                if (cur.closed && cur.points.size() > 1 &&
                    cur.points.front() == cur.points.back()) {
                    cur.points.pop_back();
                }
                out.push_back(std::move(cur));
            }
            cur = {};
        };
        for (const Step& step : rotated) {
            const Edge& e = edges[step.edge];
            if (e.seg == kVirtual) {
                flush(false);
                continue;
            }
            std::vector<Vec2um> pts = comp.segments[static_cast<std::size_t>(e.seg)].centerline;
            // Le sens de la polyligne : de start_node vers end_node ; `forward`
            // = on part de u (= start_node du segment d'origine).
            if (!step.forward) {
                std::reverse(pts.begin(), pts.end());
            }
            cur.points.insert(cur.points.end(), cur.points.empty() ? pts.begin() : pts.begin() + 1,
                              pts.end());
        }
        flush(!anyVirtual);
    }

    for (const std::size_t si : standalone) {
        const ContourSegment& s = comp.segments[si];
        if (s.centerline.size() >= 2) {
            out.push_back({s.centerline, s.closed});
        }
    }
    st.trails = out.size();
    if (stats != nullptr) {
        *stats = st;
    }
    return out;
}

} // namespace openstitch::autodigitize
