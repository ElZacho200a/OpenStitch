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

} // namespace openstitch::auto_satin::detail
