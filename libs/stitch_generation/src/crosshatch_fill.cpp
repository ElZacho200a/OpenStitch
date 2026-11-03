// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/crosshatch_fill.hpp"

#include <algorithm>
#include <numbers>

namespace openstitch::stitch_generation {

std::vector<FillStitch> fill_crosshatch(const geometry::PathSet& region,
                                        const document::CrosshatchFillParams& params) {
    const int layers = std::clamp(params.layers, 2, 3);
    std::vector<FillStitch> out;
    for (int k = 0; k < layers; ++k) {
        document::TatamiParams layer;
        layer.angle = Angle{params.angle.radians + std::numbers::pi * double(k) / double(layers)};
        layer.row_spacing = Micrometers{std::clamp(params.row_spacing.value, 300, 10'000)};
        layer.stitch_length = Micrometers{std::clamp(params.stitch_length.value, 1'000, 7'000)};
        layer.inset = Micrometers{0};
        layer.hidden_underpath = params.hidden_underpath;
        std::vector<FillStitch> part = fill_tatami(region, layer);
        if (part.empty()) {
            continue;
        }
        if (!out.empty()) {
            part.front().jump = true;
            part.front().travel = false;
        }
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

} // namespace openstitch::stitch_generation
