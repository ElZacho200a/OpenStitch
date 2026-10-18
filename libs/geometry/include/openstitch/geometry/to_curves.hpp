// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

struct ToCurvesOptions {
    // Un sommet dont le changement de direction (angle de braquage, en
    // degrés, 0 = tout droit) dépasse ce seuil reste un COIN. En dessous il
    // devient un nœud Lisse. 0 : tous les sommets restent des coins ; >= 180 :
    // tous deviennent lisses.
    double corner_angle_deg{60.0};
    // Longueur de chaque poignée, en fraction du segment adjacent (0..0,5).
    // 1/3 donne l'arrondi classique d'une spline cardinale.
    double handle_ratio{1.0 / 3.0};
};

// Convertit un polygone en courbes (HP-VEC-008) : les sommets doux reçoivent
// des tangentes de Bézier colinéaires (direction de la corde voisin
// précédent -> voisin suivant), les coins francs sont conservés. La courbe
// passe EXACTEMENT par tous les sommets d'origine. Fonction pure et
// déterministe ; les extrémités d'un tracé ouvert restent des coins ; les
// nœuds qui portent déjà des tangentes, et les sommets voisins confondus, sont
// laissés tels quels.
[[nodiscard]] Path convert_to_curves(const Path& path, const ToCurvesOptions& options = {});

[[nodiscard]] PathSet convert_to_curves(const PathSet& set, const ToCurvesOptions& options = {});

} // namespace openstitch::geometry
