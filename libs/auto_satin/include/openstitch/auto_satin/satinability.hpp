// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "openstitch/auto_satin/distance_field.hpp"
#include "openstitch/auto_satin/skeleton_graph.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

enum class SatinabilityStatus {
    Suitable,               // convertible directement
    SuitableWithWarnings,   // convertible mais à vérifier
    RequiresDecomposition,  // forme branchée : plusieurs colonnes
    Ambiguous,              // direction non déterminée (quasi circulaire)
    Unsuitable              // à refuser (trou, trop large, invalide)
};

struct SatinabilityIssue {
    std::string message;
};

struct SatinabilityReport {
    SatinabilityStatus status{SatinabilityStatus::Unsuitable};
    double confidence{0.0};

    double area_mm2{0.0};
    double perimeter_mm{0.0};
    double estimated_length_mm{0.0};
    double mean_width_mm{0.0};
    double minimum_width_mm{0.0};
    double maximum_width_mm{0.0};
    double width_variation{0.0};  // (max-min)/moyenne

    int hole_count{0};
    int branch_count{0};    // arêtes du squelette
    int endpoint_count{0};
    int junction_count{0};

    bool is_elongated{false};
    bool has_wide_area{false};
    bool has_ambiguous_direction{false};

    std::vector<SatinabilityIssue> issues;
};

struct SatinabilityThresholds {
    Micrometers min_satin_width{800};    // 0,8 mm
    Micrometers max_satin_width{12'000};  // 12 mm
    double min_elongation{2.5};          // longueur/largeur en dessous = ambigu
};

// Évalue la satinabilité à partir de la région, du graphe de squelette et du
// champ de distance. Ne génère rien ; produit un diagnostic.
[[nodiscard]] SatinabilityReport evaluate_satinability(const geometry::PathSet& region,
                                                       const SkeletonGraph& graph,
                                                       const DistanceField& distance,
                                                       const SatinabilityThresholds& thresholds);

[[nodiscard]] const char* to_string(SatinabilityStatus status);

}  // namespace openstitch::auto_satin
