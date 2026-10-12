// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>

#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/stipple/stipple.hpp"

namespace openstitch::autodigitize {

// Stippling : une image en niveaux de gris devient UN trait continu de points
// droits dont la densité reproduit les tons (Secord 2002 + tour TSP).
struct StippleObjectOptions {
    Millimeters mm_per_px{25.4 / 96.0};
    stipple::DensityOptions density{};
    stipple::StippleOptions stipple{};
    Micrometers max_link{8'000};
    // Couleur du fil (noir par défaut : l'image est lue comme un dessin à l'encre).
    std::array<std::uint8_t, 3> rgb{0, 0, 0};
};

struct StippleMetrics {
    std::size_t points{0};
    std::size_t polylines{0};
    double tour_length_mm{0.0};
    double initial_tour_length_mm{0.0};
    int lloyd_iterations{0};
};

// Image -> un objet vectoriel (chemins ouverts) + un objet brodé Running.
[[nodiscard]] Result<AutoResult> auto_digitize_stipple(const image::Image& img,
                                                       IdGenerator<ObjectId>& ids,
                                                       const StippleObjectOptions& options,
                                                       StippleMetrics* metrics = nullptr);

} // namespace openstitch::autodigitize
