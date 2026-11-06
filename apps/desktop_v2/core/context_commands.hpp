// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- tableau « contexte -> commandes » (docs/desktop-v2/16-ergonomie-gestes.md) :
// déclare UNE fois quelles commandes servent près de la sélection. Chaque surface (clic droit, puis
// barre flottante, menu radial) lit ce tableau au lieu de tenir sa propre liste de préfixes. Sans
// Qt.

#include <span>
#include <string_view>
#include <vector>

namespace openstitch::desktop_v2 {

enum class CommandSurface { ContextMenu };

// Vrai si la commande `id` doit apparaître sur la surface donnée (le grisage reste décidé par
// CommandEntry::enabled ; l'annulation/rétablissement ont leurs propres entrées et en sont exclus).
[[nodiscard]] bool commandBelongsTo(CommandSurface surface, std::string_view id);

// Ce que la sélection courante permet de dire : un ou plusieurs de ces contextes sont actifs.
enum class CommandContext { Object, Text, Satin, Nodes };

// Commandes proposées par la barre flottante pour les contextes actifs : ordre d'apparition des
// contextes puis ordre du tableau, sans doublon. Aucun contexte actif : liste vide.
[[nodiscard]] std::vector<std::string_view>
floatingBarCommands(std::span<const CommandContext> active);

} // namespace openstitch::desktop_v2
