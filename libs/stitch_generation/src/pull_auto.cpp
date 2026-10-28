// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/pull_auto.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/stitch_generation/underlay_auto.hpp"

namespace openstitch::stitch_generation {

namespace {

Micrometers to_um(double mm) {
    return Micrometers{static_cast<std::int32_t>(std::lround(mm * 1000.0))};
}

} // namespace

Micrometers choose_fill_pull(double mean_width_mm) {
    if (!(mean_width_mm > 0.0)) {
        return Micrometers{0};
    }
    return to_um(std::clamp(0.15 + 0.03 * mean_width_mm, 0.15, 0.40));
}

Micrometers choose_satin_pull(double width_mm) {
    if (!(width_mm > 0.0)) {
        return Micrometers{0};
    }
    return to_um(std::clamp(0.05 * width_mm, 0.10, 0.35));
}

document::TatamiParams resolve_pull_compensation(const document::TatamiParams& params,
                                                 const geometry::PathSet& region) {
    if (params.pull_mode == document::PullMode::Predicted) {
        document::TatamiParams out = params;
        // La prédiction complète la compensation géométrique, elle ne la remplace pas.
        // La passe finale est appliquée séparément dans effective_sequence.
        out.pull_mode = document::PullMode::Adaptive;
        out.pull_compensation = Micrometers{0};
        return out;
    }
    if (params.pull_mode != document::PullMode::Auto) {
        return params;
    }
    document::TatamiParams out = params;
    out.pull_compensation = choose_fill_pull(measure_shape(region).mean_width_mm);
    return out;
}

document::DirectionalFillParams
resolve_pull_compensation(const document::DirectionalFillParams& params,
                          const geometry::PathSet& region) {
    if (params.pull_mode == document::PullMode::Predicted) {
        document::DirectionalFillParams out = params;
        // Conserver les fins de lignes calculées par Adaptive avant la passe prédite.
        out.pull_mode = document::PullMode::Adaptive;
        out.pull_compensation = Micrometers{0};
        return out;
    }
    if (params.pull_mode != document::PullMode::Auto) {
        return params;
    }
    document::DirectionalFillParams out = params;
    out.pull_compensation = choose_fill_pull(measure_shape(region).mean_width_mm);
    return out;
}

} // namespace openstitch::stitch_generation
