// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/units.hpp"

namespace openstitch::stitch_generation {

// Point de fixation (lock stitch) : petits points qui sécurisent le fil au
// début ou à la fin d'un objet. `None` = aucun (§12).
enum class LockType { None, BackAndForth, Triangle, MicroZigzag };

// Construit les points d'un lock ancré en `anchor`, orienté vers `toward`
// (direction de la couture). `length` = taille du lock, `passes` = répétitions.
// Renvoie une polyligne (vide si `None`). Déterministe.
[[nodiscard]] std::vector<Vec2um> lock_stitches(Vec2um anchor, Vec2um toward, LockType type,
                                                Micrometers length, int passes);

}  // namespace openstitch::stitch_generation
