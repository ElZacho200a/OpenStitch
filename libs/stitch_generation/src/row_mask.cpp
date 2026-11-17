// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/row_mask.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace openstitch::stitch_generation {

double row_threshold(std::int64_t k) {
    constexpr double kGolden = 0.6180339887498949;
    // Pour k >= 0, même valeur que `row_thresholds` du directionnel (v - floor(v)) ; ramené
    // dans [0 ; 1) pour les indices négatifs des rangées ancrées.
    const double v = std::fmod(static_cast<double>(k) * kGolden, 1.0);
    const double r = v < 0.0 ? v + 1.0 : v;
    return r >= 1.0 ? 0.0 : r;
}

DensityRowMask::DensityRowMask(Field density, Slice slice)
    : density_(std::move(density)), slice_(slice) {}

double DensityRowMask::coverage(Vec2um p) const {
    const double raw = density_ ? density_(p) : 0.0;
    return std::isfinite(raw) ? std::clamp(raw, 0.0, 1.0) : 0.0;
}

bool DensityRowMask::keeps(Vec2um p, double threshold) const {
    const double d = coverage(p);
    return slice_ == Slice::Lower ? threshold < d : threshold >= 1.0 - d;
}

} // namespace openstitch::stitch_generation
