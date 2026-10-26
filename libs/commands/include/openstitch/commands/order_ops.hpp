// SPDX-License-Identifier: Apache-2.0
#pragma once

// Ordre d'empilement des objets de broderie (HP-OBJ-011) : fonctions pures qui calculent le
// nouvel ordre de couture ; la commande annulable est `ReorderEmbroideryCommand`. Dernier cousu =
// dessus (premier plan), premier cousu = dessous (arrière-plan).

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "openstitch/core/ids.hpp"

namespace openstitch::commands {

enum class StackMove { ToFront, ToBack, Forward, Backward };

// Nouvel ordre de `order` après avoir déplacé les éléments de `selected` :
//  - ToFront / ToBack : le bloc sélectionné passe à la fin / au début, ordre relatif conservé ;
//  - Forward / Backward : chaque bloc contigu avance / recule d'un cran, sans passer sur un autre
//    élément sélectionné.
// Les identifiants de `selected` absents de `order` sont ignorés. Renvoie `order` tel quel si rien
// ne bouge.
[[nodiscard]] inline std::vector<ObjectId> stacked_order(const std::vector<ObjectId>& order,
                                                         const std::vector<ObjectId>& selected,
                                                         StackMove move) {
    std::vector<ObjectId> out = order;
    const auto isSelected = [&](ObjectId id) {
        return std::find(selected.begin(), selected.end(), id) != selected.end();
    };
    switch (move) {
    case StackMove::ToFront:
        std::stable_partition(out.begin(), out.end(), [&](ObjectId id) { return !isSelected(id); });
        break;
    case StackMove::ToBack:
        std::stable_partition(out.begin(), out.end(), isSelected);
        break;
    case StackMove::Forward:
        for (std::size_t i = out.size(); i-- > 1;) {
            if (isSelected(out[i - 1]) && !isSelected(out[i])) {
                std::swap(out[i - 1], out[i]);
            }
        }
        break;
    case StackMove::Backward:
        for (std::size_t i = 1; i < out.size(); ++i) {
            if (isSelected(out[i]) && !isSelected(out[i - 1])) {
                std::swap(out[i - 1], out[i]);
            }
        }
        break;
    }
    return out;
}

} // namespace openstitch::commands
