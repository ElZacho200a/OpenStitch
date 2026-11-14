// SPDX-License-Identifier: Apache-2.0
#include "object_labels.hpp"

#include <array>

namespace openstitch::desktop_v2 {

std::string_view stitchTypeId(const document::EmbroideryObject& object) {
    // Même ordre que `document::StitchParams` (voir embroidery_object.hpp).
    static constexpr std::array<std::string_view, 15> kIds{
        "running", "tatami",       "satin",        "directional", "auto_satin",
        "blanket", "contour_fill", "motif_run",    "motif_fill",  "radial",
        "spiral",  "crosshatch",   "cross_stitch", "applique",    "multicolor_gradient"};
    const auto index = object.params.index();
    return index < kIds.size() ? kIds[index] : std::string_view{"unknown"};
}

} // namespace openstitch::desktop_v2
