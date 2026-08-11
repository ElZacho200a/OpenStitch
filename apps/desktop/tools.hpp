// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace openstitch::desktop {

// Modes d'interaction du canevas (pas des actions). L'outil actif détermine ce
// que fait un clic / glisser dans la vue. La logique fine de sélection reste
// dans MainWindow ; ce type ne fait que nommer le mode.
enum class Tool {
    Select,        // sélectionner région/objet (défaut) ; le glisser déplace la vue
    Pan,           // déplacer la vue uniquement (pas de sélection)
    Rect,          // sélection rectangulaire / recadrage image
    DrawRectangle,  // dessine un nouvel objet vectoriel rectangulaire (glisser)
    DrawEllipse,    // dessine un nouvel objet vectoriel elliptique (glisser ; Maj = cercle)
    DrawPolygon,    // dessine un nouvel objet vectoriel polygonal (clics successifs, segments droits)
    DrawPolygonRegular,  // polygone RÉGULIER (nombre de côtés réglable) inscrit dans le cadre glissé
    DrawBezier,     // dessine un nouvel objet vectoriel en courbes (clic = coin, clic-glisser = nœud lisse)
    DrawFreeform,   // dessine un nouvel objet vectoriel à main levée (glisser continu)
    DrawSatinColumn,  // colonne satin manuelle (clics alternés rail A / rail B)
};

}  // namespace openstitch::desktop
