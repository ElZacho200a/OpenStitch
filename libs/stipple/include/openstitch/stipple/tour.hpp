// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <vector>

namespace openstitch::stipple {

struct PointD {
    double x{0.0};
    double y{0.0};
};

// Optimisation d'un tour de voyageur de commerce (TSP) EUCLIDIEN FERMÉ.
//
// Tour initial par courbe de Hilbert, puis 2-opt à listes de voisins (K plus
// proches) avec « don't look bits », puis Or-opt (déplacement de 1 à 3 points
// consécutifs, avec ou sans inversion), répétés jusqu'à plus d'amélioration ou
// `max_passes`. Algorithmes classiques (Croes 1958, Or 1976, Johnson &
// McGeoch 1997) réimplémentés ici ; le solveur Concorde (licence académique
// seulement) n'est PAS utilisé. Déterministe.
struct TourOptions {
    int neighbors{8};   // K plus proches voisins par point (2..32)
    int max_passes{50}; // passes 2-opt + Or-opt
};

struct TourResult {
    std::vector<std::size_t> order; // permutation des indices d'entrée (tour fermé)
    double initial_length{0.0};     // longueur du tour de Hilbert
    double length{0.0};             // longueur finale du tour fermé
    int passes{0};
};

[[nodiscard]] TourResult optimize_tour(const std::vector<PointD>& points,
                                       const TourOptions& options = {});

// Longueur d'un tour fermé donné par `order`.
[[nodiscard]] double tour_length(const std::vector<PointD>& points,
                                 const std::vector<std::size_t>& order);

} // namespace openstitch::stipple
