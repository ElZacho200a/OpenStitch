// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Pot de peinture vectoriel : renvoie la zone VIDE fermée qui contient `seed`, délimitée par
// `obstacles` (les formes existantes). C'est la face du complémentaire de l'union des obstacles :
// les îlots d'obstacles enfermés dans la zone en deviennent des trous.
//
// Échecs (Result en erreur, message destiné à l'utilisateur) :
// - `obstacles` sans surface : rien ne délimite la zone ;
// - `seed` sur une forme existante : il n'y a pas de vide à remplir à cet endroit ;
// - la zone n'est pas fermée (elle rejoint l'extérieur du dessin) : un remplissage serait infini.
//
// `grow` dilate les obstacles de cette distance avant de chercher la zone, puis la zone trouvée
// est redilatée de la même distance : cela referme les fentes plus fines que 2 × `grow` qui
// laisseraient sinon fuir le remplissage vers l'extérieur. 0 = frontières exactes.
// Déterministe.
[[nodiscard]] Result<PathSet> fill_gap_at(const std::vector<PathSet>& obstacles, Vec2um seed,
                                          Micrometers grow = Micrometers{0});

} // namespace openstitch::geometry
