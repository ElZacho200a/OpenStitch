// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>

#include "openstitch/core/error.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::formats {

// SVG de diagnostic : couture en trait noir, sauts en pointillés orange,
// changements de couleur marqués d'un cercle rouge. Unités : millimètres.
// Sert au débogage humain ET aux tests golden (sortie déterministe, lisible
// dans les diffs Git).
[[nodiscard]] std::string to_diagnostic_svg(const stitch::StitchSequence& sequence);

[[nodiscard]] Result<void> write_svg_file(const std::filesystem::path& path,
                                          const stitch::StitchSequence& sequence);

}  // namespace openstitch::formats
