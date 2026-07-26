// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

// Corpus de formes procédurales pour le diagnostic et les tests (§29).
// Noms : rectangle, capsule, ribbon, s, y, t, cross, circle, ring,
// wide, tiny. Renvoie nullopt si le nom est inconnu.
[[nodiscard]] std::optional<geometry::PathSet> make_shape(const std::string& name);

}  // namespace openstitch::auto_satin
