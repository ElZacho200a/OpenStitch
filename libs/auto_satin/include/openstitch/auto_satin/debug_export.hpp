// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <utility>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

// SVG de diagnostic superposant : contour source (noir), trous (rouge), arêtes
// du squelette (bleu), extrémités (vert) et jonctions (orange), et
// éventuellement les rails « actuels » (`rails_from_contour`, gris pointillé)
// pour comparaison. Coordonnées en millimètres.
[[nodiscard]] std::string to_debug_svg(
    const geometry::PathSet& region, const AutoSatinAnalysis& analysis,
    const std::optional<std::pair<geometry::Path, geometry::Path>>& current_rails = std::nullopt);

}  // namespace openstitch::auto_satin
