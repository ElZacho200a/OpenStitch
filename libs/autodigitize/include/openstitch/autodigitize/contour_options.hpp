// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "openstitch/core/units.hpp"

namespace openstitch::autodigitize {

// Strategie "Contours / Line Art" : technique de couture des traits.
enum class ContourTechnique : std::uint8_t {
    Automatic, // classification par segment en point droit simple/triple
    Running,   // force le point droit sur les lignes mediennes
    Satin,     // legacy/deprecated : degrade en point droit, jamais en satin auto
};

struct ContourOptions {
    // Niveau de detail continu 0..1 (0 = tres simplifie, 1 = fidele). 0,5 =
    // reglages historiques d'auto-numerisation. Voir `contour_thresholds`.
    double detail{0.5};
    ContourTechnique technique{ContourTechnique::Automatic};
    Millimeters mm_per_px{25.4 / 96.0};
    // Meme semantique que AutoOptions::skip_largest_region (fond presume).
    bool skip_largest_region{false};
};

// Seuils concrets derives de `detail` (fonction pure, monotone : un detail
// plus eleve ne rend JAMAIS un seuil plus grand).
struct ContourThresholds {
    Micrometers min_branch_length{0};   // branche terminale gardee au moins aussi longue
    Micrometers min_loop_perimeter{0};  // boucle fermee gardee au moins aussi grande
    Micrometers min_isolated_length{0}; // element isole (sans jonction) garde au moins aussi long
    Micrometers simplify_tolerance{0};  // Douglas-Peucker sur les lignes mediennes
    Micrometers merge_distance{0};      // traits plus proches que cela sont fusionnes
};

// Garde-fous PHYSIQUES, independants de `detail` (jamais abaisses par lui),
// issus des constantes existantes : seuils de largeur historiques et
// RunningStitchParams::min_length (longueur de point minimale), pas DST.
struct ContourLimits {
    Micrometers min_satin_width{0};
    Micrometers max_satin_width{0};
    Micrometers min_element_length{0}; // sous cette longueur : aucun point possible
    Micrometers min_loop_perimeter{0}; // 3 points minimum espaces de min_element_length
    Micrometers min_simplify_tolerance{0};
    // Heuristiques historiques gardees pour compatibilite des seuils ; le mode
    // contour ne planifie plus de satin automatiquement.
    double max_width_variation{0.5}; // (max-min)/moyenne
    double max_turn_deg{60.0};       // virage brusque sur ~1 mm
};

[[nodiscard]] ContourLimits contour_limits();

// detail hors [0,1] est borne ; NaN vaut 0,5.
[[nodiscard]] ContourThresholds contour_thresholds(double detail);

} // namespace openstitch::autodigitize
