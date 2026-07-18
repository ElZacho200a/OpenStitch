// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_generation {

// Assemble la séquence machine complète du projet : les objets de broderie
// dans leur ordre, un Jump vers le début de chaque contour, un ColorChange
// entre deux objets de couleurs différentes, End à la fin.
// Fonction pure : recalculable à tout moment depuis le document (ADR-014).
[[nodiscard]] Result<stitch::StitchSequence> generate_sequence(const document::Project& project);

}  // namespace openstitch::stitch_generation
