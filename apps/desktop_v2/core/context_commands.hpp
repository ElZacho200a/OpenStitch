// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- tableau « contexte -> commandes » (docs/desktop-v2/16-ergonomie-gestes.md) :
// déclare UNE fois quelles commandes servent près de la sélection. Chaque surface (clic droit, puis
// barre flottante, menu radial) lit ce tableau au lieu de tenir sa propre liste de préfixes. Sans
// Qt.

#include <string_view>

namespace openstitch::desktop_v2 {

enum class CommandSurface { ContextMenu };

// Vrai si la commande `id` doit apparaître sur la surface donnée (le grisage reste décidé par
// CommandEntry::enabled ; l'annulation/rétablissement ont leurs propres entrées et en sont exclus).
[[nodiscard]] bool commandBelongsTo(CommandSurface surface, std::string_view id);

} // namespace openstitch::desktop_v2
