// SPDX-License-Identifier: Apache-2.0
// Façade des stratégies d'auto-satin et mesures comparables entre stratégies
// (docs/auto-satin-research.md, « Plusieurs algorithmes au choix », lot A). Une seule
// stratégie est livrée : le squelette raster actuel, qui sert de référence. Une stratégie
// n'est ajoutée à `SatinStrategy` que lorsqu'elle est réellement livrée.
#pragma once

#include <string_view>
#include <vector>

#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

enum class SatinStrategy {
    Skeleton, // Zhang-Suen, axes, traversées orientées (moteur actuel, défaut)
};

// Identifiant stable (CLI, rapports) et libellé lisible (interface).
[[nodiscard]] std::string_view strategy_id(SatinStrategy strategy);
[[nodiscard]] std::string_view strategy_label(SatinStrategy strategy);
[[nodiscard]] std::vector<SatinStrategy> available_strategies();

// Génère les traversées de la région avec la stratégie demandée. Le défaut reproduit
// exactement `generate_skeleton_satin`. Déterministe.
[[nodiscard]] Result<SkeletonSatinResult> generate_satin(SatinStrategy strategy,
                                                         const geometry::PathSet& region,
                                                         const SkeletonSatinParameters& params);

// Mesures géométriques du résultat d'une stratégie, comparables d'une stratégie à l'autre.
// Elles décrivent les traversées AVANT finitions : une bonne couverture ici ne prouve pas
// que la séquence exportée (points, sauts, coupes) reste correcte.
struct SatinMetrics {
    int columns{0};
    int crossings{0};
    int crossing_pairs{0};       // paires de traversées d'une même colonne qui se coupent
    double min_crossing_mm{0.0}; // longueur de la plus courte traversée
    double mean_crossing_mm{0.0};
    double max_crossing_mm{0.0}; // plus longue traversée (borne du point sans fractionnement)
    double max_return_mm{0.0};   // plus long retour B_i -> A_{i+1} du zigzag
    double coverage_ratio{0.0};  // 0 à 1
    double overlap_ratio{0.0};   // 1 = aucun fil en double
    int overlap_peak{0};         // pic de densité (triangles de fil sur un pixel de 100 µm)
    double uncovered_area_mm2{0.0};
    Vec2um uncovered_min{}; // boîte des manques, valide si `uncovered_area_mm2 > 0`
    Vec2um uncovered_max{};
    int refusals{0}; // messages de refus ou d'avertissement
};

// Mesure un résultat déjà généré. La couverture n'est lue que si le résultat l'a mesurée
// (`measure_coverage`) ; `measure_satin` génère puis mesure en une fois.
[[nodiscard]] SatinMetrics measure_result(const SkeletonSatinResult& result);
[[nodiscard]] Result<SatinMetrics> measure_satin(SatinStrategy strategy,
                                                 const geometry::PathSet& region,
                                                 SkeletonSatinParameters params);

} // namespace openstitch::auto_satin
