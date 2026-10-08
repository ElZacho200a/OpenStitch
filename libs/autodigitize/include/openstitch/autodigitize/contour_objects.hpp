// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/autodigitize/contour_network.hpp"
#include "openstitch/autodigitize/contour_options.hpp"

namespace openstitch::autodigitize {

// STRATEGIE : comment coudre un segment detecte.
enum class ContourStrategy : std::uint8_t {
    SingleRun, // point droit simple (trait tres fin)
    TripleRun, // point triple (trait fin mais visible)
    Rejected,  // impossible physiquement : non cousu, avec diagnostic
};

struct SegmentPlan {
    ContourStrategy strategy{ContourStrategy::Rejected};
    // Vrai si une option legacy demandait du satin et qu'elle a ete degradee.
    bool fallback{false};
    std::string reason; // toujours renseigne pour un repli ou un rejet
};

// Fonction pure : classe un segment selon la technique et les garde-fous.
[[nodiscard]] SegmentPlan classify_segment(const ContourSegment& segment,
                                           ContourTechnique technique, const ContourLimits& limits);

struct ContourMetrics {
    std::size_t components{0};
    std::size_t segments{0}; // segments detectes (apres nettoyage)
    std::size_t junctions{0};
    std::size_t endpoints{0};
    std::size_t removed_short_branches{0};
    std::size_t removed_small_elements{0}; // boucles + elements isoles sous les seuils
    double running_length_mm{0.0};
    double satin_length_mm{0.0}; // legacy, toujours 0 en auto-broderie
    double min_width_mm{0.0};
    double max_width_mm{0.0};
    double mean_width_mm{0.0}; // pondere par la longueur, segments cousus
    std::size_t fallbacks{0};  // options legacy/degradations -> point droit
    std::size_t rejected{0};   // segments / composantes refuses (garde-fous)
};

// Reseau -> objets editables : une couleur = un groupe contigu (les plus
// claires d'abord, la plus sombre en dernier), ordre deterministe. Les lignes
// sont des objets Running dont le vecteur est un chemin OUVERT (closed=false).
[[nodiscard]] Result<AutoResult> build_contour_objects(const ContourNetwork& network,
                                                       IdGenerator<ObjectId>& ids,
                                                       const ContourOptions& options,
                                                       ContourMetrics* metrics = nullptr);

// analyze_contours + build_contour_objects.
[[nodiscard]] Result<AutoResult> auto_digitize_contours(const segmentation::Segmentation& seg,
                                                        IdGenerator<ObjectId>& ids,
                                                        const ContourOptions& options,
                                                        ContourMetrics* metrics = nullptr);

} // namespace openstitch::autodigitize
