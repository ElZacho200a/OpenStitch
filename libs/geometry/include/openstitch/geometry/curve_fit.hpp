// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

struct CurveFitOptions {
    // Écart maximal (distance de Hausdorff approchée, dans les deux sens)
    // entre le chemin d'entrée et les courbes ajustées. <= 0 : sans effet.
    // Sur un contour de pixels (escalier de ±0,5 px, soit jusqu'à ~0,7 px en
    // diagonale), une tolérance inférieure à ~1 px ne peut qu'ajuster le bruit
    // de la trame : elle redonne alors autant de nœuds qu'une simplification
    // Douglas-Peucker. Prendre au moins ~1,2 px pour tirer profit des courbes.
    Micrometers tolerance{200};
    // Un sommet est un COIN (nœud anguleux, tangentes non liées) si le
    // changement de direction mesuré sur une fenêtre de part et d'autre dépasse
    // cet angle, en degrés (0..180).
    double corner_angle_deg{55.0};
    // Demi-longueur (en multiples de `tolerance`) de la fenêtre de mesure des
    // coins et des tangentes ; la fenêtre est aussi bornée à périmètre/24 pour
    // qu'un petit cercle ne soit pas pris pour une suite de coins.
    double window_in_tolerances{5.0};
};

// Ajuste des courbes de Bézier cubiques sur un chemin FERMÉ ou OUVERT fait de
// segments droits (typiquement un contour vectorisé non simplifié).
//
// Méthode : détection des coins, puis ajustement de Schneider (« An Algorithm
// for Automatically Fitting Digitized Curves », Graphics Gems, 1990 : moindres
// carrés sur les longueurs des poignées, reparamétrisation de Newton-Raphson,
// découpe récursive au point d'écart maximal). L'écart est vérifié par une
// mesure géométrique réelle, pas seulement par le paramètre. Les parties
// droites restent des segments (aucune tangente) ; les jonctions internes
// d'une courbe sont des nœuds Lisses (tangentes colinéaires), les coins des
// nœuds Coin. Déterministe. Implémentée d'après les publications (Potrace,
// sous GPL, n'est ni utilisé ni copié).
//
// Les chemins de moins de 4 nœuds sont rendus inchangés. Le nombre de nœuds du
// résultat est en général bien inférieur à celui d'une simplification
// Douglas-Peucker de même tolérance sur les formes arrondies.
[[nodiscard]] Path fit_curves(const Path& polygon, const CurveFitOptions& options = {});

// Écart de Hausdorff approché (µm) entre deux chemins une fois aplatis : outil
// de mesure pour valider l'ajustement (tests, diagnostics).
[[nodiscard]] double hausdorff_distance_um(const Path& a, const Path& b);

} // namespace openstitch::geometry
