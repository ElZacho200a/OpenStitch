// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <numbers>
#include <vector>

#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Rectangle fermé aux quatre coins droits, depuis deux coins opposés
// quelconques (ordre indifférent). Dégénéré (largeur ou hauteur nulle)
// autorisé : renvoie un chemin plat, à l'appelant de refuser si indésirable.
[[nodiscard]] Path rectangle_path(Vec2um corner1, Vec2um corner2);

// Ellipse fermée inscrite dans le rectangle défini par deux coins opposés
// quelconques (ordre indifférent) — un cercle si les deux côtés sont égaux.
// Approximation standard à 4 nœuds lisses (tangentes Bézier, constante de
// Kappa ≈ 0,5523) : erreur radiale relative maximale ~0,027 %, largement sous
// la résolution machine. Dégénérée (rayon nul sur un axe) autorisée : renvoie
// un chemin plat.
[[nodiscard]] Path ellipse_path(Vec2um corner1, Vec2um corner2);

// Polygone fermé à angles droits reliant les sommets dans l'ordre donné
// (aucun lissage, aucune simplification). Moins de 3 sommets -> chemin vide
// (rien à fermer), à l'appelant de refuser.
[[nodiscard]] Path polygon_path(const std::vector<Vec2um>& vertices);

// Polygone RÉGULIER à `sides` côtés, inscrit dans le cercle de centre
// `center` et de rayon `radius` (chaque sommet est exactement à `radius` du
// centre — définition par cercle circonscrit, la plus intuitive quand le
// rayon vient d'un geste de glisser). `start_angle` (radians, sens
// trigonométrique, 0 = est) place le premier sommet ; par défaut +π/2 (nord
// -- repère Y vers le haut, ADR-003) pour un rendu visuellement « droit »
// (ex. hexagone à sommet en haut). Moins de 3 côtés ou rayon nul -> chemin
// vide, mêmes conventions que `polygon_path`/`ellipse_path`.
[[nodiscard]] Path regular_polygon_path(Vec2um center, Micrometers radius, int sides,
                                        double start_angle = std::numbers::pi / 2.0);

// Forme dessinée à main levée (lasso) : ferme un tracé continu de points
// bruts (un par évènement de déplacement souris pendant le glisser, donc
// potentiellement très dense) puis le simplifie par Douglas-Peucker
// (`tolerance`, cf. `simplify`) pour ne garder qu'un contour à angles droits
// exploitable, sans lissage. Moins de 3 sommets après simplification ->
// chemin vide (rien à fermer), à l'appelant de refuser, comme `polygon_path`.
[[nodiscard]] Path freeform_path(const std::vector<Vec2um>& points, Micrometers tolerance);

}  // namespace openstitch::geometry
