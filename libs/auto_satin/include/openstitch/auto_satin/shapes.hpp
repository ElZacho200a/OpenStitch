// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

// Corpus de formes procédurales pour le diagnostic et les tests (§29).
// Noms : rectangle, capsule, ribbon, s, y, t, cross, h, circle, ring,
// wide, tiny, notch, pinch, trident. Corpus de torture (mission de
// durcissement du contrat SatinPlanner, 2026-08-17, §9-14), formes
// délibérément difficiles : star5, asymmetric_star, comb, E, deep_recursive,
// multi_neck, dumbbell, deep_channel, two_holes, ring_branch,
// junction_with_hole, polygonal_cut_fixture. Renvoie nullopt si le nom est
// inconnu.
[[nodiscard]] std::optional<geometry::PathSet> make_shape(const std::string& name);

}  // namespace openstitch::auto_satin
