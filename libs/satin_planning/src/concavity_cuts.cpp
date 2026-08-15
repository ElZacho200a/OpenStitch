// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/concavity_cuts.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/cut.hpp"
#include "openstitch/satin_planning/region_oracle.hpp"
#include "openstitch/satin_planning/region_split.hpp"

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
double cross(Vec2d a, Vec2d b) { return a.x * b.y - a.y * b.x; }

Vec2d normalize(Vec2d a) {
    const double n = norm(a);
    return n > 1e-9 ? scale(a, 1.0 / n) : Vec2d{0.0, 0.0};
}

Vec2um to_vec2um(Vec2d v) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(v.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(v.y))}};
}

double signed_area(const std::vector<Vec2d>& poly) {
    double area = 0.0;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) area += cross(poly[i], poly[(i + 1) % n]);
    return area * 0.5;
}

// Meme test point-dans-polygone (ray casting) que `region_split.cpp` -- pas
// factorise en utilitaire partage, meme convention que le reste du module.
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

// Sommet REFLEX (concave) du contour EXTERIEUR, avec la magnitude (en
// degres, toujours positive) de son virage local -- plus grande = concavite
// plus prononcee, plus probablement une vraie encoche qu'un pixel de bruit
// de tracage/segmentation.
struct ReflexVertex {
    std::size_t index{0};
    double turn_deg{0.0};
};

// Sommets REFLEX du contour EXTERIEUR, FILTRES par magnitude de virage
// minimale et TRIES par magnitude decroissante -- meme algorithme, meme
// convention que `auto_satin::satin_column.cpp::reflex_vertices`
// (winding-independant : un sommet est reflex si son virage local est de
// signe OPPOSE au sens global du contour), reimplemente ici car la fonction
// d'origine est privee a ce fichier (`Poly`/`P2` locaux, jamais exposes via
// un en-tete partage). Le filtre par angle (`minTurnDeg`) est necessaire en
// pratique (2026-08-14, corpus reel) : un contour issu de vectorisation
// compte souvent des dizaines de sommets "reflex" au sens strict mais
// geometriquement negligeables (quasi rectilignes), qui feraient exploser
// combinatoirement la famille concavite->concavite sans representer une
// vraie encoche.
std::vector<ReflexVertex> reflex_vertices(const geometry::Path& outer, double minTurnDeg) {
    std::vector<ReflexVertex> out;
    const std::size_t n = outer.nodes.size();
    if (n < 3) return out;
    std::vector<Vec2d> poly;
    poly.reserve(n);
    for (const auto& node : outer.nodes) poly.push_back(to_vec2d(node.pos));

    const double orientSign = signed_area(poly) >= 0.0 ? 1.0 : -1.0;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2d& prev = poly[(i + n - 1) % n];
        const Vec2d& cur = poly[i];
        const Vec2d& next = poly[(i + 1) % n];
        const Vec2d e1 = sub(cur, prev);
        const Vec2d e2 = sub(next, cur);
        const double turn = cross(e1, e2);
        if (turn * orientSign >= 0.0) continue;  // pas reflex

        const double len1 = norm(e1);
        const double len2 = norm(e2);
        if (len1 < 1e-6 || len2 < 1e-6) continue;  // arete degeneree
        // atan2(sin, cos) donne une magnitude d'angle fiable meme pour un
        // virage proche de 180 degres (contrairement a un simple asin).
        const double sinTurn = turn / (len1 * len2);
        const double cosTurn = (e1.x * e2.x + e1.y * e2.y) / (len1 * len2);
        const double turnDeg = std::abs(std::atan2(sinTurn, cosTurn)) * 180.0 / std::numbers::pi;
        if (turnDeg < minTurnDeg) continue;
        out.push_back({i, turnDeg});
    }
    std::sort(out.begin(), out.end(), [](const ReflexVertex& a, const ReflexVertex& b) { return a.turn_deg > b.turn_deg; });
    return out;
}

// Construit et evalue un candidat pour le segment `a`-`b` (memes regles que
// `region_split.cpp::generate_cut_candidates` : exactement 2 morceaux,
// aucun trop petit). `family` est un texte diagnostique, jamais utilise
// pour decider.
ConcavityCutCandidate try_segment(const geometry::PathSet& region, Vec2um a, Vec2um b, const ConcavityCutParams& params,
                                   std::string family) {
    ConcavityCutCandidate cand;
    cand.a = a;
    cand.b = b;
    cand.family = std::move(family);

    const auto cutResult = geometry::cut_path_set(region, a, b, params.cut_width);
    if (!cutResult.has_value()) {
        cand.rejection_reason = "echec de la decoupe geometrique";
        return cand;
    }
    if (cutResult->size() != 2) {
        cand.rejection_reason =
            "coupe n'a pas produit exactement 2 morceaux (" + std::to_string(cutResult->size()) + ")";
        return cand;
    }
    const double areaA = geometry::path_set_area_um2((*cutResult)[0]) / 1e6;
    const double areaB = geometry::path_set_area_um2((*cutResult)[1]) / 1e6;
    if (areaA < params.min_piece_area_mm2 || areaB < params.min_piece_area_mm2) {
        cand.rejection_reason = "fragment trop petit";
        return cand;
    }

    cand.valid = true;
    cand.first_piece = (*cutResult)[0];
    cand.second_piece = (*cutResult)[1];
    cand.first_piece_area_mm2 = areaA;
    cand.second_piece_area_mm2 = areaB;
    return cand;
}

}  // namespace

std::vector<ConcavityCutCandidate> generate_concavity_cut_candidates(const geometry::PathSet& region,
                                                                       const ConcavityCutParams& params) {
    std::vector<ConcavityCutCandidate> candidates;
    const auto& outer = region.outer;
    std::vector<ReflexVertex> reflex = reflex_vertices(outer, params.min_reflex_turn_deg);
    if (reflex.empty()) return candidates;
    // Deja tries par magnitude decroissante (`reflex_vertices`) : tronquer
    // ici garde les concavites les plus prononcees -- garde-fou de defense
    // en profondeur au-dela du filtre d'angle, borne les paires
    // concavite->concavite a max_reflex_vertices*(max_reflex_vertices-1)/2
    // dans le pire cas quelle que soit la complexite reelle du contour.
    if (reflex.size() > params.max_reflex_vertices) reflex.resize(params.max_reflex_vertices);

    // Famille concavite->concavite : chaque PAIRE de sommets reflex, coupee
    // en ligne droite entre les deux -- le cas d'une entaille en V ou d'un
    // sablier (les deux "epaules" de l'entaille SONT les deux concavites).
    for (std::size_t i = 0; i < reflex.size(); ++i) {
        for (std::size_t j = i + 1; j < reflex.size(); ++j) {
            const Vec2um a = outer.nodes[reflex[i].index].pos;
            const Vec2um b = outer.nodes[reflex[j].index].pos;
            // Repli de sante : le point median du segment doit tomber DANS
            // la region -- sinon le segment "coupe dans le vide" (traverse
            // majoritairement l'exterieur de la forme) plutot qu'a travers
            // la matiere, un candidat sans rapport avec l'intention de
            // cette famille.
            const Vec2d mid = scale(add(to_vec2d(a), to_vec2d(b)), 0.5);
            if (!path_set_contains(region, to_vec2um(mid))) {
                ConcavityCutCandidate cand;
                cand.a = a;
                cand.b = b;
                cand.family = "concavite->concavite";
                cand.rejection_reason = "point median hors de la region";
                candidates.push_back(std::move(cand));
                continue;
            }
            candidates.push_back(try_segment(region, a, b, params, "concavite->concavite"));
        }
    }

    // Famille concavite->bord oppose : chaque sommet reflex seul, coupe
    // selon la direction perpendiculaire moyenne des deux aretes qui s'y
    // rejoignent -- orientee EMPIRIQUEMENT vers l'interieur de la matiere
    // (jamais supposee depuis le sens de parcours du contour, verifiee point
    // par point : la convention gauche/droite d'une normale d'arete depend
    // du sens local, qui peut differer d'une concavite a l'autre sur une
    // forme non convexe).
    const std::size_t n = outer.nodes.size();
    for (const auto& rv : reflex) {
        const std::size_t idx = rv.index;
        const Vec2d prev = to_vec2d(outer.nodes[(idx + n - 1) % n].pos);
        const Vec2d cur = to_vec2d(outer.nodes[idx].pos);
        const Vec2d next = to_vec2d(outer.nodes[(idx + 1) % n].pos);
        const Vec2d d1 = normalize(sub(cur, prev));
        const Vec2d d2 = normalize(sub(next, cur));
        const Vec2d avgNormal = normalize(add(Vec2d{-d1.y, d1.x}, Vec2d{-d2.y, d2.x}));
        if (avgNormal.x == 0.0 && avgNormal.y == 0.0) continue;  // aretes degenerees, rien a tester

        const Vec2d probeForward = add(cur, scale(avgNormal, 10.0));
        const bool forwardInside = path_set_contains(region, to_vec2um(probeForward));
        const Vec2d inward = forwardInside ? avgNormal : Vec2d{-avgNormal.x, -avgNormal.y};

        const Vec2um a = to_vec2um(sub(cur, scale(inward, params.probe_distance_um)));
        const Vec2um b = to_vec2um(add(cur, scale(inward, params.probe_distance_um)));
        candidates.push_back(try_segment(region, a, b, params, "concavite->bord oppose"));
    }

    return candidates;
}

std::optional<std::size_t> select_best_concavity_cut(const std::vector<ConcavityCutCandidate>& candidates,
                                                       const auto_satin::SatinColumnsParameters& genParams,
                                                       const satin_coverage::SatinCoverageConfig& coverageConfig,
                                                       Micrometers density, std::size_t max_candidates_evaluated) {
    std::optional<std::size_t> bestIndex;
    double bestCoverage = -1.0;
    std::size_t evaluated = 0;

    for (std::size_t i = 0; i < candidates.size() && evaluated < max_candidates_evaluated; ++i) {
        const auto& cand = candidates[i];
        if (!cand.valid) continue;
        ++evaluated;

        SatinRegion firstRegion;
        firstRegion.region = cand.first_piece;
        firstRegion.area_mm2 = cand.first_piece_area_mm2;
        SatinRegion secondRegion;
        secondRegion.region = cand.second_piece;
        secondRegion.area_mm2 = cand.second_piece_area_mm2;

        const RegionGenerationVerdict firstVerdict = evaluate_region_generation(firstRegion, genParams, coverageConfig, density);
        const RegionGenerationVerdict secondVerdict =
            evaluate_region_generation(secondRegion, genParams, coverageConfig, density);
        if (!firstVerdict.build_succeeded || !secondVerdict.build_succeeded) continue;

        const double firstCov = firstVerdict.coverage ? firstVerdict.coverage->raw_coverage_ratio : 0.0;
        const double secondCov = secondVerdict.coverage ? secondVerdict.coverage->raw_coverage_ratio : 0.0;
        const double totalArea = cand.first_piece_area_mm2 + cand.second_piece_area_mm2;
        const double combined =
            totalArea > 0.0 ? (firstCov * cand.first_piece_area_mm2 + secondCov * cand.second_piece_area_mm2) / totalArea
                             : 0.0;
        if (combined > bestCoverage) {
            bestCoverage = combined;
            bestIndex = i;
        }
    }
    return bestIndex;
}

std::string format_concavity_cut_candidates(const std::vector<ConcavityCutCandidate>& candidates) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << "[SGSD §14 suite -- coupes ancrees sur une concavite du contour]\n\n";
    for (const auto& c : candidates) {
        out << "  " << c.family << " : ";
        if (c.valid) {
            out << "valide (piece1=" << c.first_piece_area_mm2 << "mm2, piece2=" << c.second_piece_area_mm2 << "mm2)";
        } else {
            out << "rejetee (" << c.rejection_reason << ")";
        }
        out << "\n";
    }
    out << "\nTotal : " << candidates.size() << " candidat(s)\n";
    return out.str();
}

}  // namespace openstitch::satin_planning
