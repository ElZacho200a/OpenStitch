// SPDX-License-Identifier: Apache-2.0
// Compensation du tirage (HP-STI-033) pour les remplissages sans axe de rangée unique (spirale,
// concentrique, motif) : la région est agrandie de `pull` vers l'extérieur avant le remplissage,
// donc les extrémités du tracé dépassent du contour de `pull`. Interne à la bibliothèque.
#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation::detail {

inline constexpr std::int32_t kMaxPullUm = 3'000;

[[nodiscard]] inline std::int32_t clamp_pull(Micrometers pull) {
    return std::clamp<std::int32_t>(pull.value, 0, kMaxPullUm);
}

// Région agrandie de `pull` ; la région d'origine si `pull` = 0, si l'agrandissement échoue ou
// scinde la forme (garde-fou : jamais perdre la forme).
[[nodiscard]] inline geometry::PathSet grown_for_pull(const geometry::PathSet& region,
                                                      Micrometers pull) {
    const std::int32_t p = clamp_pull(pull);
    if (p == 0) {
        return region;
    }
    auto grown = geometry::inset_path_set(region, Micrometers{-p});
    if (grown && grown->size() == 1) {
        return std::move((*grown)[0]);
    }
    return region;
}

} // namespace openstitch::stitch_generation::detail
