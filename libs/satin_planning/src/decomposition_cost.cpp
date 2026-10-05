// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/decomposition_cost.hpp"

#include <sstream>

#include "openstitch/satin_planning/region_oracle.hpp"

namespace openstitch::satin_planning {

namespace {

// Cout de continuation deja calcule pour la paire (a,b) par
// pair_branches_at_junction, recupere dans JunctionPairingReport::candidates
// -- jamais recalcule. Renvoie 1.0 (penalite "aucune continuation") si la
// paire est introuvable (ne devrait pas arriver : toutes les C(n,2) paires
// sont presentes des que la jonction a >=2 aretes incidentes).
double continuation_total_of(const JunctionPairingReport& report, std::uint32_t a,
                             std::uint32_t b) {
    for (const auto& c : report.candidates) {
        if ((c.edge_a == a && c.edge_b == b) || (c.edge_a == b && c.edge_b == a)) {
            return c.cost.valid ? c.cost.total : 1.0;
        }
    }
    return 1.0;
}

double compute_continuity_cost(const DecompositionReport& topology) {
    if (topology.junctions.empty())
        return 0.0; // aucune jonction : rien a evaluer, continuite parfaite par defaut
    double sum = 0.0;
    std::size_t count = 0;
    for (const auto& jr : topology.junctions) {
        sum += (jr.selected_pair.size() == 2)
                   ? continuation_total_of(jr, jr.selected_pair[0], jr.selected_pair[1])
                   : 1.0; // "aucun trunk" : penalite fixe, jamais gratuit
        ++count;
        // § etape 4 (croix) : un second trunk simultane (jamais peuple hors
        // d'une variante "deux traversees simultanees", cf.
        // JunctionPairingReport::secondary_pair) compte comme un terme
        // SUPPLEMENTAIRE, pas a la place du premier -- la moyenne porte alors
        // sur les DEUX traversees de cette jonction, jamais sur une seule
        // arbitrairement choisie. N'affecte aucune jonction T/Y existante
        // (secondary_pair y reste toujours vide).
        if (jr.secondary_pair.size() == 2) {
            sum += continuation_total_of(jr, jr.secondary_pair[0], jr.secondary_pair[1]);
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

} // namespace

DecompositionCost evaluate_decomposition_cost(const geometry::PathSet& region,
                                              const SkeletonGraph& graph,
                                              const DecompositionCandidate& candidate,
                                              const DecompositionCostParams& params) {
    DecompositionCost cost;
    cost.continuity_cost = compute_continuity_cost(candidate.topology);

    const RegionSplitReport split =
        split_region(region, graph, candidate.topology, params.cutParams);
    const DecompositionGenerationReport generation = evaluate_decomposition_generation(
        split, params.genParams, params.coverageConfig, params.density);
    // `aggregate_coverage_ratio` est un POURCENTAGE (0-100, cf. region_oracle.cpp),
    // pas une fraction [0,1] -- jamais renormalise ailleurs dans ce fichier,
    // ne pas se laisser reprendre par la meme confusion.
    cost.coverage_cost = 1.0 - generation.aggregate_coverage_ratio / 100.0;

    cost.total = params.weights.coverage * cost.coverage_cost +
                 params.weights.continuity * cost.continuity_cost +
                 params.weights.angle * cost.angle_cost + params.weights.width * cost.width_cost +
                 params.weights.overlap * cost.overlap_cost +
                 params.weights.density * cost.density_cost +
                 params.weights.rail_quality * cost.rail_quality_cost +
                 params.weights.routing * cost.routing_cost;
    return cost;
}

std::string format_decomposition_cost_report(
    const std::vector<std::pair<DecompositionCandidate, DecompositionCost>>& scored,
    std::optional<std::size_t> selected) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << "[Refonte topologique -- cout de decomposition]\n\n";
    for (std::size_t i = 0; i < scored.size(); ++i) {
        const auto& [candidate, cost] = scored[i];
        out << "Candidat " << candidate.candidate_id << " : " << candidate.description << "\n";
        out << "    couverture: " << cost.coverage_cost << "  continuite: " << cost.continuity_cost;
        if (cost.angle_cost != 0.0)
            out << "  angle: " << cost.angle_cost;
        if (cost.width_cost != 0.0)
            out << "  largeur: " << cost.width_cost;
        if (cost.overlap_cost != 0.0)
            out << "  overlap: " << cost.overlap_cost;
        if (cost.density_cost != 0.0)
            out << "  densite: " << cost.density_cost;
        if (cost.rail_quality_cost != 0.0)
            out << "  qualite rail: " << cost.rail_quality_cost;
        if (cost.routing_cost != 0.0)
            out << "  routage: " << cost.routing_cost;
        out << "  total: " << cost.total;
        if (selected.has_value() && *selected == i)
            out << "  [SELECTED]";
        out << "\n";
    }
    out << "\nTotal : " << scored.size() << " candidat(s) evalue(s)\n";
    return out.str();
}

} // namespace openstitch::satin_planning
