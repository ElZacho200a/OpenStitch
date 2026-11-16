// SPDX-License-Identifier: Apache-2.0
#pragma once

// Empreinte « document + sélection + scène » : change quand un objet, la sélection ou la scène
// calculée change, JAMAIS pour un survol, une progression de tâche ou un message de statut (qui
// émettent pourtant StudioController::changed). Les contrôleurs dont les listes sont coûteuses
// (fils, arbre d'objets, tirage, filtres) ne rediffusent `changed` à QML que si elle change : sans
// cela chaque événement de statut recalculait toutes les listes liées.

#include <cstdint>

#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

[[nodiscard]] inline std::uint64_t documentStamp(StudioController& studio) {
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = studio.session().revision();
    for (const auto& target : studio.session().selection().targets()) {
        hash = (hash * kPrime) ^ (target.id * 31U + static_cast<std::uint64_t>(target.kind));
    }
    const auto& scene = studio.scene();
    hash = (hash * kPrime) ^ (scene ? scene->revision + 1U : 0U);
    return hash;
}

} // namespace openstitch::desktop_v2::studio
