// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Lisse un tracé existant à la demande (HP-VEC-007) : réduit le nombre de
// nœuds ET remplace les parties arrondies par des Béziers cubiques.
//
// Fonction pure : les courbes éventuelles du tracé sont d'abord aplaties
// (`flatten_to_polygon`, tolérance `tolerance`), puis `fit_curves` ajuste des
// courbes de Schneider avec l'écart maximal `tolerance` (µm) par rapport au
// tracé aplati. Les coins francs restent des coins. Les EXTRÉMITÉS d'un tracé
// ouvert sont conservées exactement (premier et dernier nœud inchangés en
// position). Déterministe. Un tracé de moins de 4 nœuds, ou une tolérance
// <= 0, est rendu inchangé.
[[nodiscard]] Path smooth_path(const Path& path, Micrometers tolerance);

// Même opération sur une région (extérieur et trous lissés séparément).
[[nodiscard]] PathSet smooth_path_set(const PathSet& set, Micrometers tolerance);

} // namespace openstitch::geometry
