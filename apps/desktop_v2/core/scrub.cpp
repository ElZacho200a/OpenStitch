// SPDX-License-Identifier: Apache-2.0
#include "scrub.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::desktop_v2 {

double scrubbedValue(double start, double dxPixels, double step, double min, double max,
                     ScrubSpeed speed) {
    if (!(min <= max) || !std::isfinite(start)) {
        return std::isfinite(min) ? min : 0.0;
    }
    const double clampedStart = std::clamp(start, min, max);
    if (!(step > 0.0) || !std::isfinite(dxPixels)) {
        return clampedStart;
    }
    const double factor =
        speed == ScrubSpeed::Coarse ? 10.0 : (speed == ScrubSpeed::Fine ? 0.1 : 1.0);
    const double unit = step * factor;
    // Nombre entier de pas (troncature vers zéro : pas de saut avant d'avoir bougé d'un pas).
    const double steps = std::trunc(dxPixels / kScrubPixelsPerStep);
    return std::clamp(clampedStart + steps * unit, min, max);
}

} // namespace openstitch::desktop_v2
