// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace openstitch::desktop {

// Modes d'interaction du canevas (pas des actions). L'outil actif détermine ce
// que fait un clic / glisser dans la vue. La logique fine de sélection reste
// dans MainWindow ; ce type ne fait que nommer le mode.
enum class Tool {
    Select,  // sélectionner région/objet (défaut) ; le glisser déplace la vue
    Pan,     // déplacer la vue uniquement (pas de sélection)
    Rect,    // sélection rectangulaire / recadrage image
};

}  // namespace openstitch::desktop
