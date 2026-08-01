// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>

#include "openstitch/document/embroidery_object.hpp"

namespace openstitch::stitch_generation {

enum class SatinGuideSide { RailA, RailB };

// Projette l'extrémité déplacée sur son rail et refuse une disposition qui ne
// progresse plus strictement sur les deux rails. Le même demi-pas de densité
// que fill_satin_columns est utilisé : une poignée acceptée ne pourra donc pas
// être fusionnée/rejetée silencieusement lors de la génération.
[[nodiscard]] std::optional<document::SatinRung> move_satin_guide_endpoint(
    const document::SatinParams& satin, std::size_t guide_index, SatinGuideSide side,
    Vec2um desired, Micrometers flatten_tolerance = Micrometers{100});

}  // namespace openstitch::stitch_generation
