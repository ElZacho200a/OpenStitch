// SPDX-License-Identifier: Apache-2.0
#pragma once

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

}  // namespace openstitch::geometry
