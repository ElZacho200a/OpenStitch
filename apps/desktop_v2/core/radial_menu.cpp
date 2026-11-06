// SPDX-License-Identifier: Apache-2.0
#include "radial_menu.hpp"

#include <cmath>
#include <numbers>

namespace openstitch::desktop_v2 {

int radialSectorAt(double dx, double dy, double innerRadius, int count) {
    if (count < 1 || count > kRadialMaxSectors || !std::isfinite(dx) || !std::isfinite(dy)) {
        return -1;
    }
    if (std::hypot(dx, dy) < innerRadius) {
        return -1;
    }
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    double angle = std::atan2(dx, -dy); // 0 = haut, sens horaire
    if (angle < 0) {
        angle += kTwoPi;
    }
    const double width = kTwoPi / count;
    return static_cast<int>(std::floor((angle + width / 2.0) / width)) % count;
}

int radialNeighbour(int current, int step, int count) {
    if (count <= 0) {
        return -1;
    }
    if (current < 0 || current >= count) {
        return step >= 0 ? 0 : count - 1;
    }
    return ((current + (step >= 0 ? 1 : -1)) % count + count) % count;
}

int radialSectorForDigit(int digit, int count) {
    return (digit >= 1 && digit <= count && digit <= kRadialMaxSectors) ? digit - 1 : -1;
}

} // namespace openstitch::desktop_v2
