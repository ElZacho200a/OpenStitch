// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>

#include "openstitch/document/embroidery_object.hpp"

namespace openstitch::stitch_generation {

enum class SatinGuideSide { RailA, RailB };

struct SatinGuideInsertion {
    document::SatinRung guide{};
    std::size_t index{};
};

// Projette l'extrémité déplacée sur son rail et refuse une disposition qui ne
// progresse plus strictement sur les deux rails. Le même demi-pas de densité
// que fill_satin_columns est utilisé : une poignée acceptée ne pourra donc pas
// être fusionnée/rejetée silencieusement lors de la génération.
[[nodiscard]] std::optional<document::SatinRung> move_satin_guide_endpoint(
    const document::SatinParams& satin, std::size_t guide_index, SatinGuideSide side,
    Vec2um desired, Micrometers flatten_tolerance = Micrometers{100});

// Construit un guide au milieu du plus grand intervalle médian entre guides
// existants. L'index retourné est celui d'insertion dans SatinParams::rungs.
[[nodiscard]] std::optional<SatinGuideInsertion> make_satin_guide_in_largest_gap(
    const document::SatinParams& satin,
    Micrometers flatten_tolerance = Micrometers{100});

}  // namespace openstitch::stitch_generation
