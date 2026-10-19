// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Pinceau de déformation du contour (HP-VEC-018). Fonctions pures, Qt-free, en micromètres
// entiers, déterministes. Un « coup de pinceau » (stroke) est une suite de touches (dab) ;
// l'appelant applique chaque touche à la géométrie courante et ne crée UNE commande qu'au
// relâchement.

enum class BrushMode : std::uint8_t {
    Push,  // pousse / tire les nœuds de la zone selon le déplacement du curseur
    Smooth // rapproche chaque nœud de la zone du milieu de ses voisins (efface une irrégularité)
};

struct BrushDab {
    Vec2um center{};       // position du curseur
    double radius_um{0.0}; // rayon d'influence
    double strength{1.0};  // intensité dans [0, 1]
    BrushMode mode{BrushMode::Push};
    Vec2um delta{}; // Push : déplacement du curseur depuis la touche précédente
};

// Poids d'influence d'un nœud à la distance `d` du centre : lissage cubique, 1 au centre, 0 au
// rayon et au-delà, multiplié par l'intensité.
[[nodiscard]] double brush_weight(double distance_um, double radius_um, double strength);

// Applique une touche à un chemin.
//   1. Les segments de la zone plus longs que `max(rayon / 3, 300 µm)` sont subdivisés EXACTEMENT
//      (De Casteljau : courbe inchangée) pour avoir de quoi se déformer, dans la limite de
//      `max_new_nodes` nœuds ajoutés par appel.
//   2. Push : chaque nœud à moins du rayon bouge de `delta · poids` ; Smooth : vers le milieu de
//   ses
//      voisins, `poids` fois la distance. Tangentes inchangées, extrémités d'un tracé ouvert fixes.
//   3. Les nœuds droits (sans tangente, entre deux segments droits) de la zone devenus redondants
//      (écart à la corde ≤ `merge_tolerance_um`) sont retirés : le nombre de nœuds ne dérive pas
//      avec les passages répétés.
// Rayon ou intensité nuls, ou chemin à moins de 2 nœuds : chemin inchangé.
[[nodiscard]] Path apply_brush_dab(const Path& path, const BrushDab& dab,
                                   std::size_t max_new_nodes = 64,
                                   double merge_tolerance_um = 30.0);

// Même opération sur tous les chemins (extérieur et trous) d'une région.
[[nodiscard]] PathSet apply_brush_dab(const PathSet& set, const BrushDab& dab,
                                      std::size_t max_new_nodes = 64,
                                      double merge_tolerance_um = 30.0);

// Vrai si le coup de pinceau `after` n'introduit aucun croisement absent de `before`.
[[nodiscard]] bool brush_result_is_valid(const PathSet& before, const PathSet& after,
                                         Micrometers tolerance);

} // namespace openstitch::geometry
