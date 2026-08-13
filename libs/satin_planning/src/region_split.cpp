// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/region_split.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <tuple>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/cut.hpp"

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
double norm(Vec2d a) { return std::sqrt(a.x * a.x + a.y * a.y); }

Vec2um to_vec2um(Vec2d v) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(v.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(v.y))}};
}

// Point + tangente locale (unitaire) sur `edge`, a la distance `distanceUm`
// depuis l'extremite touchant la jonction (`atStart` : jonction = noeud
// `from`, parcours dans l'ordre ; sinon parcours inverse). Renvoie
// `valid=false` si la branche est plus courte que `distanceUm`.
struct PointAndTangent {
    Vec2d point{};
    Vec2d tangent{};
    bool valid{false};
};

PointAndTangent point_at_distance(const SkeletonEdge& edge, bool atStart, double distanceUm) {
    PointAndTangent out;
    const std::size_t n = edge.centerline.size();
    if (n < 2) return out;
    auto pointAt = [&](std::size_t i) { return atStart ? edge.centerline[i] : edge.centerline[n - 1 - i]; };

    double accumulated = 0.0;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const Vec2d p0 = to_vec2d(pointAt(i));
        const Vec2d p1 = to_vec2d(pointAt(i + 1));
        const Vec2d seg = sub(p1, p0);
        const double segLen = norm(seg);
        if (segLen < 1e-9) continue;
        if (accumulated + segLen >= distanceUm) {
            const double t = (distanceUm - accumulated) / segLen;
            out.point = add(p0, scale(seg, t));
            out.tangent = scale(seg, 1.0 / segLen);
            out.valid = true;
            return out;
        }
        accumulated += segLen;
    }
    return out;  // branche plus courte que distanceUm
}

// Test point-dans-polygone par ray casting (nombre de croisements), sur les
// sommets `pos` du chemin -- suffisant ici : les chemins manipules par cette
// phase (fixtures, sorties de `geometry::cut_path_set`) sont tous
// polygonaux (nœuds Coin), jamais des courbes de controle.
bool point_in_polygon(const geometry::Path& path, Vec2um p) {
    const auto& nodes = path.nodes;
    const std::size_t n = nodes.size();
    if (n < 3) return false;
    const double px = static_cast<double>(p.x.value);
    const double py = static_cast<double>(p.y.value);
    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = static_cast<double>(nodes[i].pos.x.value);
        const double yi = static_cast<double>(nodes[i].pos.y.value);
        const double xj = static_cast<double>(nodes[j].pos.x.value);
        const double yj = static_cast<double>(nodes[j].pos.y.value);
        const bool crosses = (yi > py) != (yj > py);
        if (crosses && px < (xj - xi) * (py - yi) / (yj - yi) + xi) {
            inside = !inside;
        }
    }
    return inside;
}

bool path_set_contains(const geometry::PathSet& set, Vec2um p) {
    if (!point_in_polygon(set.outer, p)) return false;
    for (const auto& hole : set.holes) {
        if (point_in_polygon(hole, p)) return false;
    }
    return true;
}

// Point sonde interieur au trace d'un SatinPath, robuste aux coupes qui ont
// pu servir a l'isoler (celles-ci n'affectent que ses deux extremites) : le
// noeud median s'il y en a un de strictement interne, sinon le milieu de
// ses deux extremites.
Vec2um probe_point(const SkeletonGraph& graph, const SatinPath& path) {
    if (path.nodes.size() >= 3) {
        const std::uint32_t midNodeId = path.nodes[path.nodes.size() / 2];
        if (const SkeletonNode* n = find_node(graph, midNodeId)) return n->position;
    }
    return to_vec2um(scale(add(to_vec2d(path.start), to_vec2d(path.end)), 0.5));
}

}  // namespace

std::vector<CutCandidate> generate_cut_candidates(const geometry::PathSet& piece, const SkeletonGraph& graph,
                                                    std::uint32_t junctionNode, std::uint32_t edgeId,
                                                    const CutCandidateParams& params) {
    std::vector<CutCandidate> candidates;
    const SkeletonEdge* edge = find_edge(graph, edgeId);
    if (edge == nullptr) return candidates;
    const bool atStart = edge->from == junctionNode;

    const std::uint32_t farNodeId = atStart ? edge->to : edge->from;
    const SkeletonNode* farNode = find_node(graph, farNodeId);
    const bool verifySatinability = params.verify_isolated_endpoint_branches && farNode != nullptr &&
                                     farNode->type == SkeletonNodeType::Endpoint;

    for (double d = params.search_min_um; d <= params.search_max_um; d += params.search_step_um) {
        const PointAndTangent sample = point_at_distance(*edge, atStart, d);
        CutCandidate cand;
        cand.distance_from_junction_um = d;
        if (!sample.valid) {
            cand.rejection_reason = "branche plus courte que la distance testee";
            candidates.push_back(cand);
            break;  // aucune distance plus grande ne sera valide non plus
        }
        cand.point = to_vec2um(sample.point);
        // Normale locale : rotation 90 deg de la tangente. `cut_path_set`
        // prolonge de toute facon le segment tres au-dela de la region ; sa
        // longueur ici n'importe pas, seule sa direction compte.
        const Vec2d normal{-sample.tangent.y, sample.tangent.x};
        cand.a = to_vec2um(sub(sample.point, scale(normal, 1000.0)));
        cand.b = to_vec2um(add(sample.point, scale(normal, 1000.0)));

        const auto cutResult = geometry::cut_path_set(piece, cand.a, cand.b, params.cut_width);
        if (!cutResult.has_value()) {
            cand.rejection_reason = "echec de la decoupe geometrique";
            candidates.push_back(cand);
            continue;
        }
        if (cutResult->size() != 2) {
            cand.rejection_reason = "coupe n'a pas produit exactement 2 morceaux (" +
                                     std::to_string(cutResult->size()) + " -- traverse une zone sans rapport)";
            candidates.push_back(cand);
            continue;
        }
        const double areaA = geometry::path_set_area_um2((*cutResult)[0]) / 1e6;
        const double areaB = geometry::path_set_area_um2((*cutResult)[1]) / 1e6;
        if (areaA < params.min_piece_area_mm2 || areaB < params.min_piece_area_mm2) {
            cand.rejection_reason = "fragment trop petit";
            candidates.push_back(cand);
            continue;
        }

        // Determine lequel des deux morceaux est la branche isolee (contient
        // le noeud distal) pour renseigner les aires ET, le cas echeant,
        // pour la verification de satinabilite ci-dessous.
        std::size_t branchIdx = 0;
        if (farNode != nullptr && path_set_contains((*cutResult)[1], farNode->position)) branchIdx = 1;
        const std::size_t remainderIdx = 1 - branchIdx;

        if (verifySatinability) {
            const auto analysis = auto_satin::analyze_region((*cutResult)[branchIdx], {});
            const bool clean = analysis.has_value() && analysis->report.junction_count == 0;
            if (!clean) {
                cand.rejection_reason = "morceau isole encore branche apres cette coupe (jonction residuelle, "
                                         "probablement une branche voisine partiellement tranchee)";
                candidates.push_back(cand);
                continue;
            }
        }

        cand.valid = true;
        cand.branch_piece_area_mm2 = geometry::path_set_area_um2((*cutResult)[branchIdx]) / 1e6;
        cand.remainder_piece_area_mm2 = geometry::path_set_area_um2((*cutResult)[remainderIdx]) / 1e6;
        candidates.push_back(cand);
    }
    return candidates;
}

RegionSplitReport split_region(const geometry::PathSet& region, const SkeletonGraph& graph,
                                const DecompositionReport& decomposition, const CutCandidateParams& params) {
    RegionSplitReport report;

    struct Event {
        std::uint32_t junction;
        std::uint32_t edge;
    };
    std::vector<Event> events;
    for (const auto& jr : decomposition.junctions) {
        for (auto edgeId : jr.detached) events.push_back({jr.node, edgeId});
    }
    std::sort(events.begin(), events.end(),
              [](const Event& a, const Event& b) { return std::tie(a.junction, a.edge) < std::tie(b.junction, b.edge); });

    std::vector<geometry::PathSet> pieces{region};

    for (const auto& ev : events) {
        CutAttempt attempt;
        attempt.junction = ev.junction;
        attempt.edge = ev.edge;

        const SkeletonNode* junctionNode = find_node(graph, ev.junction);
        const SkeletonEdge* edge = find_edge(graph, ev.edge);
        if (junctionNode == nullptr || edge == nullptr) {
            report.cuts.push_back(std::move(attempt));
            continue;
        }

        std::optional<std::size_t> pieceIdx;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (path_set_contains(pieces[i], junctionNode->position)) {
                pieceIdx = i;
                break;
            }
        }
        if (!pieceIdx) {
            report.cuts.push_back(std::move(attempt));
            continue;  // jonction introuvable dans le pool courant : incoherence, on laisse tel quel
        }

        attempt.candidates = generate_cut_candidates(pieces[*pieceIdx], graph, ev.junction, ev.edge, params);
        const auto validIt =
            std::find_if(attempt.candidates.begin(), attempt.candidates.end(), [](const CutCandidate& c) { return c.valid; });
        if (validIt == attempt.candidates.end()) {
            report.cuts.push_back(std::move(attempt));
            continue;  // aucune coupe valide : la branche reste fusionnee avec ce morceau
        }
        attempt.selected = static_cast<std::size_t>(std::distance(attempt.candidates.begin(), validIt));

        const auto cutResult = geometry::cut_path_set(pieces[*pieceIdx], validIt->a, validIt->b, params.cut_width);
        if (!cutResult.has_value() || cutResult->size() != 2) {
            report.cuts.push_back(std::move(attempt));
            continue;  // garde-fou : ne devrait pas arriver, deja verifie par generate_cut_candidates
        }

        const std::uint32_t farNodeId = edge->from == ev.junction ? edge->to : edge->from;
        const SkeletonNode* farNode = find_node(graph, farNodeId);
        std::size_t branchIdx = 0;
        std::size_t restIdx = 1;
        if (farNode != nullptr && path_set_contains((*cutResult)[1], farNode->position)) {
            branchIdx = 1;
            restIdx = 0;
        }

        geometry::PathSet branchPiece = (*cutResult)[branchIdx];
        geometry::PathSet restPiece = (*cutResult)[restIdx];
        pieces[*pieceIdx] = std::move(restPiece);
        pieces.push_back(std::move(branchPiece));

        report.cuts.push_back(std::move(attempt));
    }

    std::vector<bool> pieceUsed(pieces.size(), false);
    for (std::size_t pi = 0; pi < decomposition.paths.size(); ++pi) {
        const Vec2um probe = probe_point(graph, decomposition.paths[pi]);
        std::optional<std::size_t> found;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            if (!pieceUsed[i] && path_set_contains(pieces[i], probe)) {
                found = i;
                break;
            }
        }
        if (!found) {
            report.unresolved_paths.push_back(pi);
            continue;
        }
        pieceUsed[*found] = true;
        SatinRegion region_out;
        region_out.path_index = pi;
        region_out.region = pieces[*found];
        region_out.area_mm2 = geometry::path_set_area_um2(region_out.region) / 1e6;
        report.regions.push_back(std::move(region_out));
    }

    return report;
}

std::string format_region_split_report(const RegionSplitReport& report, const DecompositionReport& decomposition) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);

    out << "[SGSD phase 3 -- decoupage de polygone]\n\n";
    for (const auto& attempt : report.cuts) {
        out << "Jonction " << attempt.junction << " / arc " << attempt.edge << " :\n";
        for (std::size_t i = 0; i < attempt.candidates.size(); ++i) {
            const auto& c = attempt.candidates[i];
            const bool selected = attempt.selected.has_value() && *attempt.selected == i;
            out << "    d=" << c.distance_from_junction_um << "um : ";
            if (c.valid) {
                out << "valide (branche=" << c.branch_piece_area_mm2 << "mm2, reste=" << c.remainder_piece_area_mm2 << "mm2)";
            } else {
                out << "rejetee (" << c.rejection_reason << ")";
            }
            if (selected) out << "  [SELECTED]";
            out << "\n";
        }
        if (!attempt.selected.has_value()) out << "    -> aucune coupe valide, branche non isolee\n";
        out << "\n";
    }

    out.precision(2);
    out << "Regions :\n";
    for (const auto& r : report.regions) {
        out << "    chemin " << r.path_index << " -> region de " << r.area_mm2 << "mm2\n";
    }
    if (!report.unresolved_paths.empty()) {
        out << "Chemins non isoles :";
        for (auto pi : report.unresolved_paths) out << " " << pi;
        out << "\n";
    }
    out << "\nTotal : " << report.regions.size() << " region(s) sur " << decomposition.paths.size() << " chemin(s)\n";

    return out.str();
}

}  // namespace openstitch::satin_planning
