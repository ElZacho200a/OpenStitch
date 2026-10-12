// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cmath>

#include "openstitch/stipple/stipple.hpp"

namespace openstitch::stipple {

Result<DensityMap> density_from_image(const image::Image& img, const DensityOptions& options) {
    if (img.empty() || img.width <= 0 || img.height <= 0) {
        return fail(ErrorCategory::UserInput, "Image vide : rien à tramer");
    }
    if (options.max_dimension < 8 || options.max_dimension > 4096) {
        return fail(ErrorCategory::UserInput, "Dimension maximale de la carte invalide (8 à 4096)");
    }
    if (!(options.gamma > 0.0 && options.gamma <= 8.0)) {
        return fail(ErrorCategory::UserInput, "Gamma invalide (0 à 8)");
    }
    const int longest = std::max(img.width, img.height);
    const int block = std::max(1, (longest + options.max_dimension - 1) / options.max_dimension);

    DensityMap map;
    map.width = (img.width + block - 1) / block;
    map.height = (img.height + block - 1) / block;
    map.scale = static_cast<double>(block);
    map.values.assign(static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height),
                      0.0F);

    for (int my = 0; my < map.height; ++my) {
        for (int mx = 0; mx < map.width; ++mx) {
            double sum = 0.0;
            int count = 0;
            for (int y = my * block; y < std::min(img.height, (my + 1) * block); ++y) {
                for (int x = mx * block; x < std::min(img.width, (mx + 1) * block); ++x) {
                    const std::uint8_t* p =
                        img.rgba.data() +
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                         static_cast<std::size_t>(x)) *
                            4;
                    ++count;
                    if (p[3] < 128) {
                        continue; // transparent : densité nulle
                    }
                    const double lum = (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) / 255.0;
                    sum += options.invert ? lum : 1.0 - lum;
                }
            }
            const double mean = count > 0 ? sum / count : 0.0;
            map.values[static_cast<std::size_t>(my) * static_cast<std::size_t>(map.width) +
                       static_cast<std::size_t>(mx)] =
                static_cast<float>(std::pow(std::clamp(mean, 0.0, 1.0), options.gamma));
        }
    }
    return map;
}

} // namespace openstitch::stipple
