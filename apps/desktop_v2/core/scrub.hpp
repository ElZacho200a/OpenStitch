// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- champs « à glisser » : convertit un déplacement horizontal de la souris en valeur
// d'un réglage numérique (comme Blender ou Figma). Sans Qt, déterministe. L'alternative clavier
// reste le champ lui-même (saisie, flèches haut/bas).

namespace openstitch::desktop_v2 {

enum class ScrubSpeed { Normal, Coarse, Fine }; // Maj = grossier (×10), Ctrl = fin (÷10)

constexpr double kScrubPixelsPerStep = 6.0;

// Valeur après un déplacement de `dxPixels` depuis `start` : un pas de `step` tous les
// `kScrubPixelsPerStep` pixels (vers la droite : augmente), multiplié ou divisé selon `speed`,
// ramenée dans [min ; max]. Un `step` ou un intervalle invalide renvoient `start` borné.
[[nodiscard]] double scrubbedValue(double start, double dxPixels, double step, double min,
                                   double max, ScrubSpeed speed = ScrubSpeed::Normal);

} // namespace openstitch::desktop_v2
