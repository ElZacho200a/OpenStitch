// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/topology.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

namespace openstitch::satin_planning {

namespace {

// Tous les ids d'arete incidents a `report.node`, tries croissants -- deduit
// de selected_pair/detached (toujours une partition complete des aretes
// incidentes, cf. pair_branches_at_junction) plutot que de dupliquer un
// second parcours du graphe.
std::vector<std::uint32_t> incident_from_report(const JunctionPairingReport& report) {
    std::vector<std::uint32_t> out = report.detached;
    out.insert(out.end(), report.selected_pair.begin(), report.selected_pair.end());
    out.insert(out.end(), report.secondary_pair.begin(), report.secondary_pair.end());
    std::sort(out.begin(), out.end());
    return out;
}

// Cout d'angle pour la paire (a,b) (ordre indifferent), cherche dans les
// candidats deja calcules par pair_branches_at_junction -- jamais recalcule.
// Renvoie 1.0 (pire cas, "aucune continuation") si la paire n'a pas ete
// evaluee (ne devrait pas arriver : toutes les C(n,2) paires sont presentes).
double angle_cost_of(const JunctionPairingReport& report, std::uint32_t a, std::uint32_t b) {
    for (const auto& c : report.candidates) {
        if ((c.edge_a == a && c.edge_b == b) || (c.edge_a == b && c.edge_b == a)) {
            return c.cost.valid ? c.cost.angle_cost : 1.0;
        }
    }
    return 1.0;
}

// Les 3 partitions possibles de 4 aretes en 2 paires disjointes -- factorise
// entre `classify_junction` et `best_dual_through_partition` (§ etape 4,
// croix) pour ne jamais dupliquer cette combinatoire. Suppose `e.size()==4`
// (verifie par les deux appelants).
using DisjointPartition = std::array<std::pair<std::uint32_t, std::uint32_t>, 2>;
std::array<DisjointPartition, 3> disjoint_pair_partitions(const std::vector<std::uint32_t>& e) {
    return {{
        {{{e[0], e[1]}, {e[2], e[3]}}},
        {{{e[0], e[2]}, {e[1], e[3]}}},
        {{{e[0], e[3]}, {e[1], e[2]}}},
    }};
}

// Meilleure partition en 2 paires disjointes pour un noeud de degre EXACTEMENT
// 4, par somme des deux angle_cost -- reutilise par
// `enumerate_decomposition_candidates` pour generer la variante "deux
// traversees simultanees" (§ etape 4, croix). `incident` doit avoir
// exactement 4 elements (verifie par l'appelant) ; `report` doit contenir
// toutes les C(4,2)=6 paires (calcule par `pair_branches_at_junction` sur ce
// meme noeud, jamais recalcule ici).
DisjointPartition best_dual_through_partition(const JunctionPairingReport& report,
                                              const std::vector<std::uint32_t>& incident) {
    const auto partitions = disjoint_pair_partitions(incident);
    std::size_t bestIdx = 0;
    double bestCost = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < partitions.size(); ++i) {
        const double cost = angle_cost_of(report, partitions[i][0].first, partitions[i][0].second) +
                            angle_cost_of(report, partitions[i][1].first, partitions[i][1].second);
        if (cost < bestCost) {
            bestCost = cost;
            bestIdx = i;
        }
    }
    return partitions[bestIdx];
}

} // namespace

RegionClassification classify_edge(const SkeletonGraph& graph, const SkeletonEdge& edge,
                                   double width_jump_ratio_threshold) {
    RegionClassification out;

    const auto isJunction = [&](std::uint32_t nodeId) {
        const SkeletonNode* n = find_node(graph, nodeId);
        return n != nullptr && n->type == SkeletonNodeType::Junction;
    };
    if (isJunction(edge.from) || isJunction(edge.to)) {
        out.reasons.push_back(SingularityReason::JunctionDegreeGE3);
    }

    if (!edge.local_radii_um.empty()) {
        double minR = std::numeric_limits<double>::max();
        double maxR = 0.0;
        for (const double r : edge.local_radii_um) {
            if (r <= 0.0)
                continue; // rayon degenere (bord de squelette) : jamais la mesure de reference
            minR = std::min(minR, r);
            maxR = std::max(maxR, r);
        }
        if (minR > 0.0 && minR < std::numeric_limits<double>::max() &&
            maxR / minR >= width_jump_ratio_threshold) {
            out.reasons.push_back(SingularityReason::AbruptWidthChange);
        }
    }

    out.region_class = out.reasons.empty() ? RegionClass::Regular : RegionClass::Singular;
    return out;
}

SatinJunction classify_junction(const SkeletonGraph& graph, std::uint32_t junctionNode,
                                const ContinuationCostParams& costParams,
                                const JunctionClassificationParams& classParams) {
    SatinJunction out;
    out.node_id = junctionNode;
    if (const SkeletonNode* n = find_node(graph, junctionNode))
        out.position = n->position;

    const JunctionPairingReport report = pair_branches_at_junction(graph, junctionNode, costParams);
    out.incident_branches = incident_from_report(report);
    const std::size_t degree = out.incident_branches.size();

    if (degree == 3) {
        std::array<double, 3> angleCosts{
            angle_cost_of(report, out.incident_branches[0], out.incident_branches[1]),
            angle_cost_of(report, out.incident_branches[0], out.incident_branches[2]),
            angle_cost_of(report, out.incident_branches[1], out.incident_branches[2]),
        };
        std::array<double, 3> sorted = angleCosts;
        std::sort(sorted.begin(), sorted.end());
        const double gap = sorted[1] - sorted[0];
        const double spread = sorted[2] - sorted[0];
        if (gap >= classParams.dominant_pair_gap) {
            out.type = JunctionType::T;
        } else if (spread < classParams.symmetric_pair_closeness) {
            out.type = JunctionType::Y;
        } else {
            out.type = JunctionType::AcuteFork;
        }
    } else if (degree == 4) {
        const auto partitions = disjoint_pair_partitions(out.incident_branches);
        std::array<double, 3> partitionCost{};
        for (std::size_t i = 0; i < 3; ++i) {
            partitionCost[i] =
                angle_cost_of(report, partitions[i][0].first, partitions[i][0].second) +
                angle_cost_of(report, partitions[i][1].first, partitions[i][1].second);
        }
        std::array<double, 3> sorted = partitionCost;
        std::sort(sorted.begin(), sorted.end());
        // Meme seuil de dominance que le cas T, applique a une somme de deux
        // couts plutot qu'un seul -- echelle comparable (chaque cout reste
        // dans [0,1], la somme dans [0,2]) : premier point de depart
        // explicite, a recalibrer par shadow-log comme le reste de ce
        // fichier.
        out.type = (sorted[1] - sorted[0] >= classParams.dominant_pair_gap) ? JunctionType::X
                                                                            : JunctionType::Complex;
    } else {
        out.type = JunctionType::Complex; // degre <3 (ne devrait pas arriver) ou >=5 : hors de
                                          // portee (plan §5-6)
    }
    return out;
}

DecompositionCandidateSet
enumerate_decomposition_candidates(const SkeletonGraph& graph, std::uint32_t junctionNode,
                                   const ContinuationCostParams& params,
                                   std::size_t max_candidates_per_junction) {
    DecompositionCandidateSet out;
    out.junction_node = junctionNode;
    if (max_candidates_per_junction == 0)
        return out;

    const JunctionPairingReport natural = pair_branches_at_junction(graph, junctionNode, params);
    const std::vector<std::uint32_t> incident = incident_from_report(natural);
    std::uint32_t nextId = 0;

    // § etape 4 (croix) : un noeud de degre EXACTEMENT 4 reserve UNE place
    // pour la variante "deux traversees simultanees" (jamais generee par
    // pair_branches_at_junction elle-meme, cf. son propre commentaire dans
    // branch_pairing.hpp) -- reduit d'autant le nombre de variantes "un seul
    // trunk" testees plutot que d'augmenter `max_candidates_per_junction` au
    // cas par cas (le budget total reste celui demande par l'appelant).
    const bool reserveDualThroughSlot = incident.size() == 4 && max_candidates_per_junction > 1;

    // Reserve toujours une place pour la variante "aucun trunk" sauf si le
    // budget ne permet qu'un seul candidat (alors ce candidat unique est
    // l'argmin naturel -- comportement historique de decompose_into_paths,
    // jamais une surprise pour un appelant qui ne demande qu'un candidat).
    std::size_t pairBudget = max_candidates_per_junction > 1 ? max_candidates_per_junction - 1
                                                             : max_candidates_per_junction;
    if (reserveDualThroughSlot && pairBudget > 1)
        --pairBudget;
    for (const auto& cand : natural.candidates) {
        if (out.candidates.size() >= pairBudget)
            break;
        if (!cand.cost.valid)
            continue;
        DecompositionCandidate dc;
        dc.candidate_id = nextId++;
        dc.topology = decompose_into_paths(
            graph, params, {JunctionOverride{junctionNode, {cand.edge_a, cand.edge_b}}});
        std::ostringstream desc;
        desc.setf(std::ios::fixed);
        desc.precision(3);
        desc << "jonction " << junctionNode << " : trunk = aretes " << cand.edge_a << "/"
             << cand.edge_b << " (cout " << cand.cost.total << ")";
        dc.description = desc.str();
        out.candidates.push_back(std::move(dc));
        if (max_candidates_per_junction == 1)
            return out; // pas de variante independante : un seul candidat demande
    }

    if (reserveDualThroughSlot && out.candidates.size() < max_candidates_per_junction) {
        const DisjointPartition partition = best_dual_through_partition(natural, incident);
        DecompositionCandidate dc;
        dc.candidate_id = nextId++;
        dc.topology =
            decompose_into_paths(graph, params,
                                 {JunctionOverride{junctionNode,
                                                   {partition[0].first, partition[0].second},
                                                   {partition[1].first, partition[1].second}}});
        std::ostringstream desc;
        desc << "jonction " << junctionNode << " : deux traversees simultanees, aretes "
             << partition[0].first << "/" << partition[0].second << " et " << partition[1].first
             << "/" << partition[1].second;
        dc.description = desc.str();
        out.candidates.push_back(std::move(dc));
    }

    if (out.candidates.size() < max_candidates_per_junction) {
        DecompositionCandidate dc;
        dc.candidate_id = nextId++;
        dc.topology = decompose_into_paths(graph, params, {JunctionOverride{junctionNode, {}}});
        std::ostringstream desc;
        desc << "jonction " << junctionNode << " : aucun trunk, toutes les branches independantes";
        dc.description = desc.str();
        out.candidates.push_back(std::move(dc));
    }
    return out;
}

std::string format_decomposition_candidates_report(const DecompositionCandidateSet& candidates) {
    std::ostringstream out;
    out << "[Refonte topologique -- candidats de decomposition, jonction "
        << candidates.junction_node << "]\n\n";
    for (const auto& c : candidates.candidates) {
        out << "Candidat " << c.candidate_id << " : " << c.description << "\n";
        out << "    chemins resultants : " << c.topology.paths.size() << "\n";
    }
    out << "\nTotal : " << candidates.candidates.size() << " candidat(s)\n";
    return out.str();
}

} // namespace openstitch::satin_planning
