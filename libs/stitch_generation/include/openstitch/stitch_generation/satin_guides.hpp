// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "openstitch/document/embroidery_object.hpp"

namespace openstitch::stitch_generation {

enum class SatinGuideSide { RailA, RailB };

struct SatinGuideInsertion {
    document::SatinRung guide{};
    std::size_t index{};
};

// Classe tous les guides en une seule projection des rails et renvoie, pour
// chacun, la jonction structurelle éventuellement portée par le guide terminal.
// La détection repose sur les stations projetées, pas sur l'index du vecteur :
// un import aux rungs inversés/désordonnés reste donc interprété correctement.
[[nodiscard]] std::vector<std::optional<std::uint32_t>> satin_guide_junctions(
    const document::SatinParams& satin,
    Micrometers flatten_tolerance = Micrometers{100});

// Variante ponctuelle utilisée par les commandes et les actions contextuelles.
[[nodiscard]] std::optional<std::uint32_t> satin_guide_junction(
    const document::SatinParams& satin, std::size_t guide_index,
    Micrometers flatten_tolerance = Micrometers{100});

// Projette l'extrémité déplacée sur son rail et refuse une disposition qui ne
// progresse plus strictement sur les deux rails. Le même demi-pas de densité
// que fill_satin_columns est utilisé : une poignée acceptée ne pourra donc pas
// être fusionnée/rejetée silencieusement lors de la génération. Un guide
// terminal attaché à une jonction est structurel et ne peut pas être déplacé.
[[nodiscard]] std::optional<document::SatinRung> move_satin_guide_endpoint(
    const document::SatinParams& satin, std::size_t guide_index, SatinGuideSide side,
    Vec2um desired, Micrometers flatten_tolerance = Micrometers{100});

// Construit un guide au milieu du plus grand intervalle médian entre guides
// existants. L'index retourné est celui d'insertion dans SatinParams::rungs.
[[nodiscard]] std::optional<SatinGuideInsertion> make_satin_guide_in_largest_gap(
    const document::SatinParams& satin,
    Micrometers flatten_tolerance = Micrometers{100});

}  // namespace openstitch::stitch_generation
