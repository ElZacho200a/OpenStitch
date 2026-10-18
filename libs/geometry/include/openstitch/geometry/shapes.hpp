// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <numbers>

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Formes prédéfinies paramétriques (HP-VEC-014). Toutes sont des fonctions
// pures et déterministes en micromètres entiers (repère Y vers le haut) ;
// une entrée dégénérée renvoie un chemin VIDE (à l'appelant de refuser),
// comme `polygon_path`. Les formes fermées tournent en sens antihoraire.

// Étoile à `points` branches (>= 3), centrée en `center`. Les pointes sont sur
// le cercle de rayon `outer_radius`, les creux sur `inner_radius`
// (0 < inner < outer, sinon chemin vide). `start_angle` place la première
// pointe (radians, 0 = est ; défaut +pi/2 = nord). 2·points coins.
[[nodiscard]] Path star_path(Vec2um center, Micrometers outer_radius, Micrometers inner_radius,
                             int points, double start_angle = std::numbers::pi / 2.0);

// Cœur inscrit dans le rectangle défini par deux coins opposés quelconques :
// pointe en bas au milieu, creux en haut au milieu, deux lobes en Béziers
// cubiques. Symétrique. Largeur ou hauteur nulle -> chemin vide.
[[nodiscard]] Path heart_path(Vec2um corner1, Vec2um corner2);

// Flèche droite (7 coins) allant de `tail` à `tip`. `shaft_width` : largeur du
// corps ; `head_width` : largeur de la tête (>= shaft_width, sinon relevée à
// shaft_width) ; `head_length` : longueur de la tête, bornée à la longueur
// totale. `tail == tip` ou largeur <= 0 -> chemin vide.
[[nodiscard]] Path arrow_path(Vec2um tail, Vec2um tip, Micrometers shaft_width,
                              Micrometers head_width, Micrometers head_length);

// Rectangle à coins arrondis (quarts de cercle en Béziers cubiques). `radius`
// est borné à la moitié du plus petit côté ; un rayon <= 0 redonne
// `rectangle_path`. Largeur ou hauteur nulle -> chemin vide.
[[nodiscard]] Path rounded_rectangle_path(Vec2um corner1, Vec2um corner2, Micrometers radius);

// Arc de cercle OUVERT de centre `center`, rayon `radius`, partant de
// `start_angle` (radians, 0 = est) et balayant `sweep_angle` (positif =
// antihoraire, borné à ±2·pi). Un segment de Bézier par quart de tour au
// plus. Rayon <= 0 ou balayage nul -> chemin vide.
[[nodiscard]] Path arc_path(Vec2um center, Micrometers radius, double start_angle,
                            double sweep_angle);

// Spirale d'Archimède OUVERTE, de `start_radius` à `end_radius` en `turns`
// tours (positif = antihoraire, négatif = horaire), commençant à
// `start_angle`. Courbe lisse passant par des échantillons (12 par tour).
// turns == 0 ou rayons < 0 -> chemin vide.
[[nodiscard]] Path spiral_path(Vec2um center, Micrometers start_radius, Micrometers end_radius,
                               double turns, double start_angle = 0.0);

} // namespace openstitch::geometry
