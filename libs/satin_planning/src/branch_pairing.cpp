// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/branch_pairing.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <unordered_set>

namespace openstitch::satin_planning {

namespace {

struct Vec2d {
    double x{0.0};
    double y{0.0};
};

Vec2d to_vec2d(Vec2um v) { return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)}; }
Vec2d sub(Vec2d a, Vec2d b) { return {a.x - b.x, a.y - b.y}; }
Vec2d add(Vec2d a, Vec2d b) { return {a.x + b.x, a.y + b.y}; }
Vec2d scale(Vec2d a, double s) { return {a.x * s, a.y * s}; }
double dot(Vec2d a, Vec2d b) { return a.x * b.x + a.y * b.y; }
double norm(Vec2d a) { return std::sqrt(dot(a, a)); }
Vec2d normalized(Vec2d a) {
    const double n = norm(a);
    return n > 1e-9 ? Vec2d{a.x / n, a.y / n} : Vec2d{};
}

const SkeletonEdge* find_edge(const SkeletonGraph& graph, std::uint32_t id) {
    const auto it = std::find_if(graph.edges.begin(), graph.edges.end(),
                                  [id](const SkeletonEdge& e) { return e.id == id; });
    return it == graph.edges.end() ? nullptr : &*it;
}

const SkeletonNode* find_node(const SkeletonGraph& graph, std::uint32_t id) {
    const auto it = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                  [id](const SkeletonNode& n) { return n.id == id; });
    return it == graph.nodes.end() ? nullptr : &*it;
}

struct EdgeEndSample {
    bool valid{false};
    Vec2d outward_tangent{};  // unit, moyenne ponderee par longueur sur la fenetre
    Vec2d near_tangent{};     // unit, premier segment depuis la jonction
    Vec2d far_tangent{};      // unit, dernier segment echantillonne dans la fenetre
    double mean_radius_um{0.0};
};

// Echantillonne le comportement de `edge` sur les `windowUm` premiers
// micrometres depuis l'extremite touchant la jonction. `atStart` indique si
// la jonction est le noeud `from` (parcours dans l'ordre) ou `to` (parcours
// inverse) -- `local_radii_um` suit la meme indexation que `centerline` donc
// subit la meme inversion.
EdgeEndSample sample_edge_end(const SkeletonEdge& edge, bool atStart, double windowUm) {
    EdgeEndSample out;
    const std::size_t n = edge.centerline.size();
    if (n < 2) return out;

    const bool haveRadii = edge.local_radii_um.size() == n;
    auto pointAt = [&](std::size_t i) -> Vec2um { return atStart ? edge.centerline[i] : edge.centerline[n - 1 - i]; };
    auto radiusAt = [&](std::size_t i) -> double {
        if (!haveRadii) return 0.0;
        return atStart ? edge.local_radii_um[i] : edge.local_radii_um[n - 1 - i];
    };

    double accumulatedLen = 0.0;
    Vec2d weightedTangentSum{};
    double radiusSum = radiusAt(0);
    std::size_t radiusSamples = 1;
    bool haveNear = false;
    bool haveFar = false;
    Vec2d lastSegDir{};

    for (std::size_t i = 0; i + 1 < n; ++i) {
        const Vec2d p0 = to_vec2d(pointAt(i));
        const Vec2d p1 = to_vec2d(pointAt(i + 1));
        const Vec2d seg = sub(p1, p0);
        const double segLen = norm(seg);
        if (segLen < 1e-9) continue;
        const Vec2d segDir = scale(seg, 1.0 / segLen);

        if (!haveNear) {
            out.near_tangent = segDir;
            haveNear = true;
        }

        const double remaining = windowUm - accumulatedLen;
        if (remaining > 0.0) {
            const double take = std::min(segLen, remaining);
            weightedTangentSum = add(weightedTangentSum, scale(segDir, take));
            lastSegDir = segDir;
            haveFar = true;
        }
        accumulatedLen += segLen;
        radiusSum += radiusAt(i + 1);
        ++radiusSamples;

        if (accumulatedLen >= windowUm) break;
    }

    if (!haveNear || !haveFar) return out;  // arc degenere (longueur quasi nulle)

    out.outward_tangent = normalized(weightedTangentSum);
    out.far_tangent = lastSegDir;
    out.mean_radius_um = radiusSum / static_cast<double>(radiusSamples);
    out.valid = true;
    return out;
}

std::vector<std::uint32_t> incident_edges(const SkeletonGraph& graph, std::uint32_t node) {
    std::vector<std::uint32_t> result;
    for (const auto& e : graph.edges) {
        if (e.from == node) result.push_back(e.id);
        if (e.to == node && e.to != e.from) result.push_back(e.id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace

ContinuationCost continuation_cost(const SkeletonGraph& graph, std::uint32_t junctionNode, std::uint32_t edgeA,
                                    std::uint32_t edgeB, const ContinuationCostParams& params) {
    ContinuationCost cost;
    const SkeletonEdge* ea = find_edge(graph, edgeA);
    const SkeletonEdge* eb = find_edge(graph, edgeB);
    if (ea == nullptr || eb == nullptr) return cost;

    const bool aAtStart = ea->from == junctionNode;
    const bool bAtStart = eb->from == junctionNode;

    const EdgeEndSample sa = sample_edge_end(*ea, aAtStart, params.tangent_window_um);
    const EdgeEndSample sb = sample_edge_end(*eb, bAtStart, params.tangent_window_um);
    if (!sa.valid || !sb.valid) return cost;

    // 0 = tangentes sortantes opposees (continuation en ligne droite),
    // 1 = memes direction (repli sur soi-meme).
    const double cosTheta = std::clamp(dot(sa.outward_tangent, sb.outward_tangent), -1.0, 1.0);
    cost.angle_cost = (1.0 + cosTheta) / 2.0;

    const double ra = std::max(sa.mean_radius_um, 1.0);
    const double rb = std::max(sb.mean_radius_um, 1.0);
    cost.width_cost = std::abs(std::log(ra / rb));

    const double cosNearFarA = std::clamp(dot(sa.near_tangent, sa.far_tangent), -1.0, 1.0);
    const double cosNearFarB = std::clamp(dot(sb.near_tangent, sb.far_tangent), -1.0, 1.0);
    cost.curvature_cost = ((1.0 - cosNearFarA) + (1.0 - cosNearFarB)) / 2.0;

    cost.total = params.weights.angle * cost.angle_cost + params.weights.width * cost.width_cost +
                 params.weights.curvature * cost.curvature_cost;
    cost.valid = true;
    return cost;
}

JunctionPairingReport pair_branches_at_junction(const SkeletonGraph& graph, std::uint32_t junctionNode,
                                                 const ContinuationCostParams& params) {
    JunctionPairingReport report;
    report.node = junctionNode;

    const std::vector<std::uint32_t> incident = incident_edges(graph, junctionNode);
    if (incident.size() < 2) {
        report.detached = incident;
        return report;
    }

    for (std::size_t i = 0; i < incident.size(); ++i) {
        for (std::size_t j = i + 1; j < incident.size(); ++j) {
            PairCandidate candidate;
            candidate.edge_a = incident[i];
            candidate.edge_b = incident[j];
            candidate.cost = continuation_cost(graph, junctionNode, incident[i], incident[j], params);
            report.candidates.push_back(candidate);
        }
    }
    std::stable_sort(report.candidates.begin(), report.candidates.end(), [](const PairCandidate& a, const PairCandidate& b) {
        if (a.cost.valid != b.cost.valid) return a.cost.valid;  // invalides en fin de liste
        return a.cost.total < b.cost.total;
    });

    if (!report.candidates.empty() && report.candidates.front().cost.valid) {
        const PairCandidate& best = report.candidates.front();
        report.selected_pair = {best.edge_a, best.edge_b};
        for (auto id : incident) {
            if (id != best.edge_a && id != best.edge_b) report.detached.push_back(id);
        }
    } else {
        // Aucune paire exploitable (centerlines trop courtes) : tout reste
        // detache plutot que de choisir un appariement arbitraire.
        report.detached = incident;
    }
    return report;
}

DecompositionReport decompose_into_paths(const SkeletonGraph& graph, const ContinuationCostParams& params) {
    DecompositionReport report;
    for (const auto& node : graph.nodes) {
        if (node.type == SkeletonNodeType::Junction) {
            report.junctions.push_back(pair_branches_at_junction(graph, node.id, params));
        }
    }

    // (noeud, arc) -> arc partenaire si cet arc continue au-dela de ce noeud
    // (fait partie de la paire retenue a cette jonction).
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> continuation;
    for (const auto& jr : report.junctions) {
        if (jr.selected_pair.size() == 2) {
            continuation[{jr.node, jr.selected_pair[0]}] = jr.selected_pair[1];
            continuation[{jr.node, jr.selected_pair[1]}] = jr.selected_pair[0];
        }
    }

    std::unordered_set<std::uint32_t> visited;

    auto walkFrom = [&](std::uint32_t startEdgeId, std::uint32_t startNode) {
        SatinPath path;
        std::uint32_t currentNode = startNode;
        std::uint32_t currentEdgeId = startEdgeId;
        while (true) {
            visited.insert(currentEdgeId);
            const SkeletonEdge* edge = find_edge(graph, currentEdgeId);
            if (edge == nullptr) break;  // ne devrait pas arriver, graphe coherent par construction
            const std::uint32_t otherNode = edge->from == currentNode ? edge->to : edge->from;
            path.edges.push_back(currentEdgeId);
            path.nodes.push_back(currentNode);
            path.length_um += edge->length_um;
            currentNode = otherNode;

            const auto it = continuation.find({currentNode, currentEdgeId});
            if (it == continuation.end() || visited.count(it->second) != 0) {
                path.nodes.push_back(currentNode);
                break;
            }
            currentEdgeId = it->second;
        }
        if (const SkeletonNode* startN = find_node(graph, path.nodes.front())) path.start = startN->position;
        if (const SkeletonNode* endN = find_node(graph, path.nodes.back())) path.end = endN->position;
        report.paths.push_back(std::move(path));
    };

    // Premiere passe : demarre chaque chemin a une vraie extremite (un bout
    // qui ne continue pas), dans l'ordre deterministe des arcs du graphe.
    for (const auto& edge : graph.edges) {
        if (visited.count(edge.id) != 0) continue;
        const bool fromContinues = continuation.count({edge.from, edge.id}) != 0;
        const bool toContinues = continuation.count({edge.to, edge.id}) != 0;
        if (fromContinues && toContinues) continue;  // interne a une chaine, sera atteint depuis son vrai debut
        const std::uint32_t startNode = !fromContinues ? edge.from : edge.to;
        walkFrom(edge.id, startNode);
    }

    // Deuxieme passe (garde-fou) : arcs restants uniquement possibles si le
    // graphe contient un cycle ferme d'appariements (topologiquement
    // inattendu pour un squelette, mais on ne suppose jamais un arbre sans
    // le verifier) -- coupe arbitrairement au premier arc non visite.
    for (const auto& edge : graph.edges) {
        if (visited.count(edge.id) != 0) continue;
        walkFrom(edge.id, edge.from);
    }

    return report;
}

std::string format_decomposition_report(const SkeletonGraph& graph, const DecompositionReport& report) {
    std::ostringstream out;
    out.setf(std::ios::fixed);

    out << "[SGSD phase 1 -- appariement de branches]\n\n";
    out << "Squelette :\n";
    out << "    extremites = " << graph.endpoint_count() << "\n";
    out << "    jonctions = " << graph.junction_count() << "\n";
    out << "    arcs = " << graph.edges.size() << "\n\n";

    for (const auto& jr : report.junctions) {
        const std::vector<std::uint32_t> incident = incident_edges(graph, jr.node);
        out << "Jonction " << jr.node << " (degre " << incident.size() << ") :\n";
        out << "    candidats :\n";
        for (const auto& c : jr.candidates) {
            const bool selected = jr.selected_pair.size() == 2 && jr.selected_pair[0] == c.edge_a && jr.selected_pair[1] == c.edge_b;
            out << "        arc " << c.edge_a << " <-> arc " << c.edge_b << " : ";
            if (c.cost.valid) {
                out.precision(3);
                out << "angle=" << c.cost.angle_cost << " largeur=" << c.cost.width_cost
                    << " courbure=" << c.cost.curvature_cost << " total=" << c.cost.total;
            } else {
                out << "invalide (centerline degeneree)";
            }
            if (selected) out << "  [SELECTED]";
            out << "\n";
        }
        if (jr.selected_pair.size() == 2) {
            out << "    retenu : arc " << jr.selected_pair[0] << " <-> arc " << jr.selected_pair[1] << "\n";
        } else {
            out << "    retenu : aucun\n";
        }
        out << "    detaches :";
        for (auto id : jr.detached) out << " arc " << id;
        if (jr.detached.empty()) out << " (aucun)";
        out << "\n\n";
    }

    out << "Chemins (SatinPath) :\n";
    out.precision(2);
    for (std::size_t i = 0; i < report.paths.size(); ++i) {
        const auto& p = report.paths[i];
        out << "    chemin " << i << " : arcs [";
        for (std::size_t k = 0; k < p.edges.size(); ++k) out << (k > 0 ? ", " : "") << p.edges[k];
        out << "], noeuds [";
        for (std::size_t k = 0; k < p.nodes.size(); ++k) out << (k > 0 ? ", " : "") << p.nodes[k];
        out << "], longueur=" << (p.length_um / 1000.0) << "mm\n";
    }
    out << "\nTotal : " << report.paths.size() << " chemin(s)\n";

    return out.str();
}

}  // namespace openstitch::satin_planning
