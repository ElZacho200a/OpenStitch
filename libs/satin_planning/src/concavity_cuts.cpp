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

// Quadrilatere perpendiculaire-offset pour le segment p0->p1, meme technique
// que `geometry::cut_path_set` (cut.cpp) pour une coupe droite -- reimplemente
// ici car cette construction reste privee a cut.cpp (types Clipper2 internes,
// jamais exposes hors fichier, cf. ADR-005). `halfWidthUm` = demi-largeur de
// la bande a soustraire.
geometry::Path make_quad(Vec2d p0, Vec2d p1, double halfWidthUm) {
    const Vec2d dir = normalize(sub(p1, p0));
    const Vec2d normal{-dir.y, dir.x};
    geometry::Path quad;
    quad.closed = true;
    quad.nodes.push_back({to_vec2um(add(p0, scale(normal, halfWidthUm))), geometry::NodeType::Corner});
    quad.nodes.push_back({to_vec2um(add(p1, scale(normal, halfWidthUm))), geometry::NodeType::Corner});
    quad.nodes.push_back({to_vec2um(sub(p1, scale(normal, halfWidthUm))), geometry::NodeType::Corner});
    quad.nodes.push_back({to_vec2um(sub(p0, scale(normal, halfWidthUm))), geometry::NodeType::Corner});
    return quad;
}

// Petit carre (axis-aligned) centre sur `center` -- comble le coin NON
// COUPE que laisserait le coude (§ `cut_polyline`) : deux bandes de largeur
// `cutWidth` qui ne partagent que le point `waypoint` divergent chacune
// selon leur propre direction, laissant un triangle de matiere intacte du
// cote EXTERIEUR (convexe) du virage -- un simple prolongement le long de
// chaque bande (parallelement a sa propre direction) ne comble PAS ce
// triangle, qui est perpendiculaire aux deux bandes, pas dans leur
// prolongement. Un carre centre au point de coude, plus large que la bande
// (appele avec `halfSizeUm` = 3x la demi-largeur de coupe), ferme ce coin
// quel que soit l'angle du virage sans mitre a calculer -- au prix d'un
// arrondi grossier du coin plutot qu'un
// veritable coude anguleux, sans consequence a l'echelle d'une largeur de
// coupe de quelques dizaines de micrometres face a des morceaux mesures en
// mm².
geometry::Path make_square(Vec2d center, double halfSizeUm) {
    geometry::Path square;
    square.closed = true;
    square.nodes.push_back({to_vec2um({center.x - halfSizeUm, center.y - halfSizeUm}), geometry::NodeType::Corner});
    square.nodes.push_back({to_vec2um({center.x + halfSizeUm, center.y - halfSizeUm}), geometry::NodeType::Corner});
    square.nodes.push_back({to_vec2um({center.x + halfSizeUm, center.y + halfSizeUm}), geometry::NodeType::Corner});
    square.nodes.push_back({to_vec2um({center.x - halfSizeUm, center.y + halfSizeUm}), geometry::NodeType::Corner});
    return square;
}

// Direction locale VERS L'INTERIEUR de la matiere au sommet reflex `idx` du
// contour `outer` -- bissectrice moyenne des deux aretes qui s'y
// rejoignent, orientee EMPIRIQUEMENT (jamais supposee depuis le sens de
// parcours, verifiee par un point-test) : extraite de la famille
// concavite->bord oppose, qui en avait deja besoin pour construire un
// candidat depuis une SEULE concavite. Reutilisee pour prolonger legerement
// un segment concavite->concavite AU-DELA du sommet lui-meme (cf.
// `try_polyline`) : contrairement a une extension dans la direction
// entrante du segment (qui peut raser l'epaule voisine de la meme encoche
// avant de ressortir ailleurs, cf. commentaire de `cut_polyline`), la
// bissectrice locale est PAR CONSTRUCTION la direction la plus sure pour
// "sortir proprement" de la matiere pres de ce sommet precis.
Vec2d inward_bisector(const geometry::PathSet& region, const geometry::Path& outer, std::size_t idx) {
    const std::size_t n = outer.nodes.size();
    const Vec2d prev = to_vec2d(outer.nodes[(idx + n - 1) % n].pos);
    const Vec2d cur = to_vec2d(outer.nodes[idx].pos);
    const Vec2d next = to_vec2d(outer.nodes[(idx + 1) % n].pos);
    const Vec2d d1 = normalize(sub(cur, prev));
    const Vec2d d2 = normalize(sub(next, cur));
    const Vec2d avgNormal = normalize(add(Vec2d{-d1.y, d1.x}, Vec2d{-d2.y, d2.x}));
    if (avgNormal.x == 0.0 && avgNormal.y == 0.0) return Vec2d{0.0, 0.0};  // aretes degenerees

    const Vec2d probeForward = add(cur, scale(avgNormal, 10.0));
    const bool forwardInside = path_set_contains(region, to_vec2um(probeForward));
    return forwardInside ? avgNormal : Vec2d{-avgNormal.x, -avgNormal.y};
}

// Coupe le long de la polyligne extendedA->waypoint->extendedB (§14, dernier
// element, 2026-08-16) : un quadrilatere par segment (§ `make_quad`).
// `extendedA`/`extendedB` sont deja les points EFFECTIVEMENT utilises pour
// construire les quadrilateres -- prolonges au-dela des sommets reflex
// `a`/`b` par l'appelant (`try_polyline`) le long de leur bissectrice
// locale respective, PAS le long de la direction entrante du segment : une
// extension dans la direction entrante peut raser l'epaule voisine de la
// meme encoche avant de ressortir par un bord sans rapport, y decoupant un
// fragment parasite (constate empiriquement en construisant la forme de
// test "sablier obstrue", cf.
// `test_concavity_cuts.cpp::elbow_obstructed_hourglass_shape`) ; ne
// PAS prolonger du tout laisse a l'inverse une connexion residuelle non
// coupee pile au sommet (constate empiriquement aussi). La bissectrice
// locale est la seule direction qui garantisse de "sortir proprement" de
// la matiere pres de ce sommet precis, quel que soit l'angle du segment
// entrant.
std::optional<std::vector<geometry::PathSet>> cut_polyline(const geometry::PathSet& region, Vec2d extendedA,
                                                             Vec2d waypoint, Vec2d extendedB, Micrometers cutWidth) {
    const double halfWidthUm = static_cast<double>(cutWidth.value) / 2.0;

    std::vector<geometry::Path> quads;
    quads.push_back(make_quad(extendedA, waypoint, halfWidthUm));
    quads.push_back(make_quad(waypoint, extendedB, halfWidthUm));
    // Les deux quadrilateres ne se touchent qu'en un seul point exact
    // (`waypoint`) -- pour tout coude non parfaitement rectiligne, cela
    // laisse un triangle de matiere NON coupe du cote exterieur (convexe)
    // du virage (cf. `make_square`), et `subtract_polygons` produit alors
    // plus de deux morceaux au lieu de separer proprement la region.
    quads.push_back(make_square(waypoint, halfWidthUm * 3.0));

    auto result = geometry::subtract_polygons(region, quads);
    if (!result.has_value()) return std::nullopt;
    return *result;
}

// Cherche un COUDE (point de passage interieur) tel que les deux segments
// a->waypoint et waypoint->b restent entierement DANS la region -- utilise
// SEULEMENT quand la coupe DROITE a->b echoue (son point median tombe hors
// de la region, § concavite->concavite ci-dessous) : c'est exactement le cas
// que les trois familles precedentes ne pouvaient pas resoudre (une entaille
// dont les deux concavites ne se "voient" pas en ligne droite, ex. un
// sablier COURBE plutot qu'un sablier droit). Recherche bornee (12 pas, deux
// sens) le long de la perpendiculaire au segment a->b depuis son milieu --
// pas une resolution generale de plus court chemin, seulement le cas simple
// a UN coude.
std::optional<Vec2d> find_elbow_waypoint(const geometry::PathSet& region, Vec2d a, Vec2d b) {
    const Vec2d mid = scale(add(a, b), 0.5);
    const Vec2d dir = normalize(sub(b, a));
    const double baseLen = norm(sub(b, a));
    if (baseLen < 1e-6) return std::nullopt;
    const Vec2d normal{-dir.y, dir.x};

    constexpr int kSteps = 12;
    for (int step = 1; step <= kSteps; ++step) {
        const double offset = baseLen * (0.15 * static_cast<double>(step));
        for (double sign : {1.0, -1.0}) {
            const Vec2d candidate = add(mid, scale(normal, offset * sign));
            if (!path_set_contains(region, to_vec2um(candidate))) continue;
            const Vec2d midAW = scale(add(a, candidate), 0.5);
            const Vec2d midWB = scale(add(candidate, b), 0.5);
            if (path_set_contains(region, to_vec2um(midAW)) && path_set_contains(region, to_vec2um(midWB))) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

// Construit et evalue un candidat polygonal a->waypoint->b (memes regles
// d'acceptation que `try_segment` : exactement 2 morceaux, aucun trop
// petit) -- aucune nouvelle regle de rejet inventee ici non plus. `a` et `b`
// sont chacun prolonges d'une petite marge (`kElbowEndExtendUm`) le long de
// leur PROPRE bissectrice locale (`inward_bisector`, memes indices reflex
// que ceux qui ont produit `a`/`b`) avant de construire la coupe -- cf.
// `cut_polyline` pour la justification de ce choix de direction.
constexpr double kElbowEndExtendUm = 200.0;

ConcavityCutCandidate try_polyline(const geometry::PathSet& region, const geometry::Path& outer, std::size_t idxA,
                                    std::size_t idxB, Vec2um waypoint, const ConcavityCutParams& params,
                                    std::string family) {
    const Vec2um a = outer.nodes[idxA].pos;
    const Vec2um b = outer.nodes[idxB].pos;
    ConcavityCutCandidate cand;
    cand.a = a;
    cand.b = b;
    cand.waypoint = waypoint;
    cand.family = std::move(family);

    // `a`/`b` sont deja les sommets reflex ; le quadrilatere doit depasser
    // LEGEREMENT au-dela, dans le VIDE de l'encoche (oppose a la bissectrice
    // "vers l'interieur"), pour que sa soustraction traverse reellement le
    // contour au lieu d'y etre tangente -- exactement le meme role que le
    // premier point (`cur - inward*dist`) de la famille concavite->bord
    // oppose, qui doit lui aussi partir du VIDE pour garantir une coupe
    // complete.
    const Vec2d inwardA = inward_bisector(region, outer, idxA);
    const Vec2d inwardB = inward_bisector(region, outer, idxB);
    const Vec2d extendedA = sub(to_vec2d(a), scale(inwardA, kElbowEndExtendUm));
    const Vec2d extendedB = sub(to_vec2d(b), scale(inwardB, kElbowEndExtendUm));

    const auto cutResult = cut_polyline(region, extendedA, to_vec2d(waypoint), extendedB, params.cut_width);
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
                // §14, dernier element (2026-08-16) : avant de renoncer,
                // tente une coupe en COUDE -- exactement le cas que les
                // trois familles precedentes ne pouvaient pas resoudre (les
                // deux concavites existent bien, mais ne se "voient" pas en
                // ligne droite).
                if (params.try_elbow_cuts) {
                    if (const auto waypoint = find_elbow_waypoint(region, to_vec2d(a), to_vec2d(b))) {
                        candidates.push_back(try_polyline(region, outer, reflex[i].index, reflex[j].index,
                                                           to_vec2um(*waypoint), params,
                                                           "concavite->concavite (polygonale)"));
                        continue;
                    }
                }
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
    for (const auto& rv : reflex) {
        const std::size_t idx = rv.index;
        const Vec2d cur = to_vec2d(outer.nodes[idx].pos);
        const Vec2d inward = inward_bisector(region, outer, idx);
        if (inward.x == 0.0 && inward.y == 0.0) continue;  // aretes degenerees, rien a tester

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
