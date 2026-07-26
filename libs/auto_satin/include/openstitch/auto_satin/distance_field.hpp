// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/auto_satin/raster.hpp"

namespace openstitch::auto_satin {

// Transformée de distance : pour chaque pixel intérieur, distance (en µm) au
// contour le plus proche (≈ demi-largeur locale de la forme).
struct DistanceField {
    int width{0};
    int height{0};
    std::vector<float> distance_um;  // 0 à l'extérieur
    RasterTransform transform;

    [[nodiscard]] float at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return 0.0f;
        }
        return distance_um[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                           static_cast<std::size_t>(x)];
    }
    [[nodiscard]] float max_value() const;
};

[[nodiscard]] DistanceField distance_transform(const RasterMask& mask);

}  // namespace openstitch::auto_satin
