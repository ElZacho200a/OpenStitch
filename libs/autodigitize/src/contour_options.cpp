// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/contour_options.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "openstitch/auto_satin/graph_cleanup.hpp"
#include "openstitch/auto_satin/satinability.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/document/embroidery_object.hpp"

namespace openstitch::autodigitize {

ContourLimits contour_limits() {
    ContourLimits l;
    l.min_satin_width = auto_satin::SatinabilityThresholds{}.min_satin_width;
    l.max_satin_width = AutoOptions{}.satin_max_width;
    l.min_element_length = document::RunningStitchParams{}.min_length;
    l.min_loop_perimeter = Micrometers{3 * l.min_element_length.value};
    l.min_simplify_tolerance = Micrometers{100}; // pas DST (0,1 mm)
    return l;
}

ContourThresholds contour_thresholds(double detail) {
    const double d = std::isfinite(detail) ? std::clamp(detail, 0.0, 1.0) : 0.5;
    // 4 (detail 0) -> 1 (detail 0,5, ancres existantes) -> 0,25 (detail 1).
    const double f = std::pow(4.0, 1.0 - 2.0 * d);
    const ContourLimits lim = contour_limits();
    const auto cleanup = auto_satin::GraphCleanupParameters{};
    const AutoOptions anchors{};
    const auto scaled = [](double v) {
        return Micrometers{static_cast<std::int32_t>(std::lround(v))};
    };

    ContourThresholds t;
    t.min_branch_length =
        std::max(lim.min_element_length,
                 scaled(static_cast<double>(cleanup.minimum_branch_length.value) * f));
    // Boucle : cercle dont le diametre vaut la branche minimale.
    t.min_loop_perimeter = std::max(
        lim.min_loop_perimeter,
        scaled(std::numbers::pi * static_cast<double>(cleanup.minimum_branch_length.value) * f));
    // Element isole : deux fois la lamelle minimale de l'auto-numerisation.
    t.min_isolated_length =
        std::max(lim.min_element_length, scaled(2.0 * anchors.min_feature_width_mm * 1000.0 * f));
    t.simplify_tolerance =
        std::max(lim.min_simplify_tolerance,
                 scaled(static_cast<double>(anchors.simplify_tolerance.value) * std::sqrt(f)));
    t.merge_distance = scaled(static_cast<double>(anchors.fill_overlap.value) * f);
    return t;
}

} // namespace openstitch::autodigitize
