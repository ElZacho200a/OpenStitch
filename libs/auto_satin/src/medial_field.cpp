// SPDX-License-Identifier: Apache-2.0
#include "medial_field.hpp"

#include <algorithm>
#include <cstddef>

namespace openstitch::auto_satin::detail {

namespace {

// Ordre total utilisé pour maintenir `best` trié pendant le scan et pour le
// départage déterministe des égalités exactes de distance (poly_index puis
// edge_index puis edge_t croissants, cf. en-tête).
bool foot_order(const BoundaryFoot& a, const BoundaryFoot& b) {
    if (a.distance_um != b.distance_um) {
        return a.distance_um < b.distance_um;
    }
    if (a.poly_index != b.poly_index) {
        return a.poly_index < b.poly_index;
    }
    if (a.edge_index != b.edge_index) {
        return a.edge_index < b.edge_index;
    }
    return a.edge_t < b.edge_t;
}

} // namespace

std::vector<BoundaryFoot> nearest_boundary_feet(const std::vector<Poly>& polys, P2 p,
                                                const FootQuery& query) {
    std::vector<BoundaryFoot> best; // invariant : trié croissant par foot_order, taille <= cap
    if (query.max_feet <= 0) {
        return best;
    }
    const std::size_t cap = static_cast<std::size_t>(query.max_feet);
    best.reserve(cap);

    for (std::size_t poly_index = 0; poly_index < polys.size(); ++poly_index) {
        const Poly& poly = polys[poly_index];
        const std::size_t n = poly.size();
        if (n < 3) {
            // Polygone dégénéré : pas d'arête valable. region_polys() filtre
            // déjà ce cas en amont ; ce garde-fou protège les appels directs
            // (tests) qui construisent un Poly à la main.
            continue;
        }
        for (std::size_t edge_index = 0; edge_index < n; ++edge_index) {
            // Même formule de projection point->segment que distance_to_polys
            // / project_to_contour (geometry_detail.hpp) -- recopiée ici à
            // l'identique plutôt que partagée via un nouveau helper : le
            // fichier d'origine n'en a jamais extrait un non plus
            // (distance_to_polys et project_to_contour ont chacune leur
            // propre copie), donc on reste dans le style déjà établi plutôt
            // que d'introduire une abstraction que le code existant n'a pas
            // choisie.
            const P2 a = poly[edge_index];
            const P2 b = poly[(edge_index + 1) % n];
            const P2 ab = b - a;
            const double len2 = dot(ab, ab);
            const double t = len2 > 1e-12 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
            const P2 proj = a + ab * t;
            const double d = norm(p - proj);

            const BoundaryFoot foot{proj, poly_index, edge_index, t, d};
            const auto insert_pos = std::upper_bound(best.begin(), best.end(), foot, foot_order);
            if (best.size() < cap) {
                best.insert(insert_pos, foot);
            } else if (insert_pos != best.end()) {
                best.insert(insert_pos, foot);
                best.pop_back();
            }
        }
    }

    if (best.empty()) {
        return best;
    }
    const double min_distance = best.front().distance_um;
    const double threshold = min_distance * (1.0 + query.tolerance_relative);
    const auto cutoff =
        std::find_if(best.begin(), best.end(), [threshold](const BoundaryFoot& foot) {
            return foot.distance_um > threshold;
        });
    best.erase(cutoff, best.end());
    return best;
}

namespace {

// Applique le meme filtre de tolerance que `nearest_boundary_feet` (relatif
// au minimum DE LA LISTE PASSEE, pas a un minimum global partage entre les
// deux cotes -- c'est precisement le point de `nearest_boundary_feet_oriented` :
// chaque cote a sa propre notion de "proche").
void apply_tolerance(std::vector<BoundaryFoot>& feet, double tolerance_relative) {
    if (feet.empty()) {
        return;
    }
    const double min_distance = feet.front().distance_um;
    const double threshold = min_distance * (1.0 + tolerance_relative);
    const auto cutoff =
        std::find_if(feet.begin(), feet.end(), [threshold](const BoundaryFoot& foot) {
            return foot.distance_um > threshold;
        });
    feet.erase(cutoff, feet.end());
}

// Insere `foot` dans `best` (deja trie par `foot_order`, taille <= cap) --
// factorise la logique d'insertion bornee partagee par les deux cotes de
// `nearest_boundary_feet_oriented` (identique a la boucle d'insertion de
// `nearest_boundary_feet` ci-dessus, dupliquee ici deliberement : les deux
// fonctions restent chacune lisibles d'une seule traite plutot que de
// partager un etat mutable via une reference indirecte).
void insert_bounded(std::vector<BoundaryFoot>& best, const BoundaryFoot& foot, std::size_t cap) {
    const auto insert_pos = std::upper_bound(best.begin(), best.end(), foot, foot_order);
    if (best.size() < cap) {
        best.insert(insert_pos, foot);
    } else if (insert_pos != best.end()) {
        best.insert(insert_pos, foot);
        best.pop_back();
    }
}

} // namespace

OrientedBoundaryFeet nearest_boundary_feet_oriented(const std::vector<Poly>& polys, P2 p,
                                                    const OrientedFootQuery& query) {
    OrientedBoundaryFeet result;
    if (query.max_feet_per_side <= 0) {
        return result;
    }
    const std::size_t cap = static_cast<std::size_t>(query.max_feet_per_side);
    result.left.reserve(cap);
    result.right.reserve(cap);

    for (std::size_t poly_index = 0; poly_index < polys.size(); ++poly_index) {
        const Poly& poly = polys[poly_index];
        const std::size_t n = poly.size();
        if (n < 3) {
            // Polygone degenere : meme garde-fou que nearest_boundary_feet.
            continue;
        }
        for (std::size_t edge_index = 0; edge_index < n; ++edge_index) {
            // Meme formule de projection point->segment que nearest_boundary_feet
            // ci-dessus (copie deliberee, cf. la justification dans ce fichier).
            const P2 a = poly[edge_index];
            const P2 b = poly[(edge_index + 1) % n];
            const P2 ab = b - a;
            const double len2 = dot(ab, ab);
            const double t = len2 > 1e-12 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
            const P2 proj = a + ab * t;
            const double d = norm(p - proj);

            const BoundaryFoot foot{proj, poly_index, edge_index, t, d};
            // Classification GAUCHE/DROITE au moment de la collecte elle-meme
            // -- c'est le coeur de la correction Phase B.5 : un cote ne peut
            // plus etre exclu par un candidat plus proche de l'AUTRE cote,
            // puisque chaque cote maintient son propre ensemble borne
            // independamment, dans le MEME parcours O(E).
            const double side = dot(proj - p, query.normal);
            if (side >= 0.0) {
                insert_bounded(result.left, foot, cap);
            } else {
                insert_bounded(result.right, foot, cap);
            }
        }
    }

    apply_tolerance(result.left, query.tolerance_relative);
    apply_tolerance(result.right, query.tolerance_relative);
    return result;
}

} // namespace openstitch::auto_satin::detail
