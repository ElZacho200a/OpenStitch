// SPDX-License-Identifier: Apache-2.0
// En-tête INTERNE à libs/image.
#pragma once

#include "openstitch/image/ops.hpp"

namespace openstitch::image::detail {

// Lignes cohérentes (FDoG sur champ de tangentes de flux). Voir CoherentLinesOp.
[[nodiscard]] Result<Image> apply_coherent_lines(const Image& in, const CoherentLinesOp& op);

} // namespace openstitch::image::detail
