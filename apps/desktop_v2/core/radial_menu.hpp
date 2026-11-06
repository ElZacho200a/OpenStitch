// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- menu radial : géométrie des secteurs (sans Qt, testée). Les commandes viennent du
// même tableau « contexte -> commandes » que la barre flottante (core/context_commands.hpp) ; les
// touches 1 à 8 et les flèches sont l'alternative clavier au geste.

namespace openstitch::desktop_v2 {

constexpr int kRadialMaxSectors = 8;

// Secteur désigné par le déplacement (dx, dy) depuis le centre, en pixels écran (y vers le bas).
// Le secteur 0 est centré vers le haut, les suivants tournent dans le sens horaire. -1 si le point
// est dans le disque central (rayon < `innerRadius`) ou si `count` n'est pas dans [1 ; 8].
[[nodiscard]] int radialSectorAt(double dx, double dy, double innerRadius, int count);

// Secteur voisin pour la navigation au clavier (circulaire) ; `step` = +1 horaire, -1 anti-horaire.
// `current` = -1 (aucun) : +1 donne le premier, -1 le dernier. `count` <= 0 : -1.
[[nodiscard]] int radialNeighbour(int current, int step, int count);

// Secteur d'une touche chiffre : '1' -> 0 ... '8' -> 7 ; -1 si la touche ne désigne aucun secteur.
[[nodiscard]] int radialSectorForDigit(int digit, int count);

} // namespace openstitch::desktop_v2
