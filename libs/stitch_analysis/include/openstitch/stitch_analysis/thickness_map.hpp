// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/density_map.hpp"

namespace openstitch::stitch_analysis {

// =============================================================================
// HP-VIEW-014 : carte d'ÉPAISSEUR estimée (cœur, Qt-free, déterministe).
//
// Elle réutilise la grille de HP-VIEW-005 : par case, la longueur de fil de la séquence EFFECTIVE
// (toutes passes, donc sous-couche + dessus) x la largeur du fil / la surface de la case donne
// l'épaisseur en « couches de remplissage dense ». Cette mesure ne dépend pas du type de point :
// un satin, un tatami ou un contour se distinguent par le fil qu'ils déposent par mm².
//
// Ce module ajoute la conversion en MILLIMÈTRES et une résolution de grille adaptée au zoom.
// La conversion est une ESTIMATION, pas une mesure : `layer_thickness` (0,20 mm) est une hypothèse
// bornée par un calcul sur les données du fil (docs/veille-epaisseur-2026-10.md, §4-5) et
// l'empilement est supposé linéaire, donc optimiste (les couches réelles se tassent). À valider
// sur une machine réelle (HP-PHYS-001).
// =============================================================================

struct ThicknessOptions {
    Micrometers cell{1'000};          // côté d'une case (1 mm)
    Micrometers thread_width{400};    // largeur du fil retenue pour la mesure (0,4 mm)
    Micrometers layer_thickness{200}; // épaisseur estimée d'UNE couche dense (0,20 mm)
    double max_layers{3.0}; // au-delà : « trop épais » (même seuil que `couches-superposees`)
    std::size_t max_cells{1'000'000}; // plafond de cases : au-delà la case est doublée
};

// Épaisseur en mm d'un nombre de couches denses (empilement linéaire).
[[nodiscard]] double layers_to_mm(double layers, const ThicknessOptions& options = {});

// Grille de fil de la séquence, à la résolution demandée.
[[nodiscard]] DensityMap compute_thickness_grid(const stitch::StitchSequence& sequence,
                                                const ThicknessOptions& options = {});

// Case la plus épaisse.
struct ThicknessPeak {
    bool found{false};
    Vec2um position{}; // centre de la case
    double layers{0.0};
    double mm{0.0};
    ObjectId object{};      // objet ayant déposé le plus de fil dans la case
    bool over_limit{false}; // `layers` > `max_layers`
};

[[nodiscard]] ThicknessPeak find_thickness_peak(const stitch::StitchSequence& sequence,
                                                const DensityMap& grid,
                                                const ThicknessOptions& options = {});

// Côté de case adapté à un zoom : la plus petite valeur de {0,5 ; 1 ; 2 ; 4 ; 8 mm} qui donne au
// moins `target_cell_px` pixels d'écran par case. Zoom avant -> carte plus fine, zoom arrière ->
// carte plus grossière et donc plus rapide : l'affichage reste réactif sur un grand dessin.
// Entrée invalide (<= 0 ou non finie) : 1 mm.
[[nodiscard]] Micrometers adaptive_cell(double pixels_per_mm, int target_cell_px = 4);

} // namespace openstitch::stitch_analysis
