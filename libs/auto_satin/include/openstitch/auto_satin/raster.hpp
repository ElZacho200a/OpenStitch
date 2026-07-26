// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

// Transformation masque <-> coordonnées physiques (µm). Le pixel (0,0) est en
// haut-gauche ; Y du modèle est vers le HAUT, donc row croissant = y décroissant.
struct RasterTransform {
    double min_x_um{0.0};   // µm du bord gauche (centre du pixel col=0)
    double max_y_um{0.0};   // µm du bord haut (centre du pixel row=0)
    double pixel_size_um{50.0};

    [[nodiscard]] Vec2um to_um(double col, double row) const {
        return Vec2um{Micrometers{static_cast<std::int32_t>(min_x_um + col * pixel_size_um)},
                      Micrometers{static_cast<std::int32_t>(max_y_um - row * pixel_size_um)}};
    }
    [[nodiscard]] double col_of(Vec2um p) const {
        return (static_cast<double>(p.x.value) - min_x_um) / pixel_size_um;
    }
    [[nodiscard]] double row_of(Vec2um p) const {
        return (max_y_um - static_cast<double>(p.y.value)) / pixel_size_um;
    }
};

// Masque binaire (0 = extérieur, 1 = intérieur de la région, trous exclus).
struct RasterMask {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> pixels;
    RasterTransform transform;

    [[nodiscard]] std::uint8_t at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return 0;
        }
        return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                      static_cast<std::size_t>(x)];
    }
};

struct SkeletonRasterParameters {
    Micrometers pixel_size{50};  // 0,05 mm par défaut
    int max_dimension{1500};     // borne les dimensions du masque
    int margin_px{2};            // marge autour de la forme
};

// Rasterise une région (extérieur + trous) en masque binaire, à résolution
// physique. Déterministe. La résolution est augmentée si nécessaire pour
// respecter `max_dimension`.
[[nodiscard]] Result<RasterMask> rasterize(const geometry::PathSet& region,
                                           const SkeletonRasterParameters& params);

}  // namespace openstitch::auto_satin
