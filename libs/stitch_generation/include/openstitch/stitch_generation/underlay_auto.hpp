// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Sous-couche AUTOMATIQUE (HP-ENG-002, `UnderlayMode::Auto`) : le moteur choisit les
// sous-couches d'un objet selon son type, sa taille et sa largeur, au lieu de laisser
// l'utilisateur cocher les cases. Fonctions pures et déterministes ; les seuils sont
// ceux de `docs/source/moteur-de-points.md` (section « Sous-couche automatique ») et sont testés
// (test_underlay_auto.cpp).

// Mesures d'une forme. L'épaisseur moyenne est `2 × aire / périmètre` (trous compris
// dans le périmètre) : exacte pour une bande longue (sa largeur), égale au rayon pour un
// disque ; c'est la mesure qui décide si la forme supporte une sous-couche.
struct ShapeMetrics {
    double area_mm2{0.0};
    double perimeter_mm{0.0};
    double mean_width_mm{0.0};
};
[[nodiscard]] ShapeMetrics measure_shape(const geometry::PathSet& region);

// Remplissage (tatami, directionnel) :
//  - aire < 6 mm² ou épaisseur < 0,8 mm   : aucune sous-couche (une petite forme se
//    déforme moins que ce que coûte la sous-couche) ;
//  - aire < 60 mm²                        : contour seul ;
//  - sinon                                : contour + rangées perpendiculaires
//    (espacement 2 mm, 2,5 mm au-delà de 400 mm²).
// Le retrait du contour vaut un quart de l'épaisseur, borné à [0,3 ; 0,6] mm.
struct FillUnderlayChoice {
    bool edge{false};
    bool parallel{false};
    Micrometers inset{600};
    Micrometers spacing{2'000};
};
[[nodiscard]] FillUnderlayChoice choose_fill_underlay(const ShapeMetrics& shape);

// Satin, selon la largeur de la colonne :
//  - < 1,0 mm     : aucune ;
//  - [1,0 ; 3,5[  : centre ;
//  - [3,5 ; 7,0[  : contour (deux chemins internes) ;
//  - >= 7,0 mm    : contour + zigzag léger.
struct SatinUnderlayChoice {
    bool center{false};
    bool edge{false};
    bool zigzag{false};
};
[[nodiscard]] SatinUnderlayChoice choose_satin_underlay(double width_mm);

// Largeur moyenne (mm) d'une colonne satin à deux rails : aire entre les rails divisée
// par la longueur moyenne des rails. 0 si les rails sont dégénérés.
[[nodiscard]] double satin_mean_width_mm(const geometry::Path& rail_a,
                                         const geometry::Path& rail_b);

// Paramètres EFFECTIFS d'un remplissage pour une région : copie inchangée en mode
// `Manual`, sous-couches choisies par `choose_fill_underlay` en mode `Auto`.
[[nodiscard]] document::TatamiParams resolve_underlay(const document::TatamiParams& params,
                                                      const geometry::PathSet& region);
[[nodiscard]] document::DirectionalFillParams
resolve_underlay(const document::DirectionalFillParams& params, const geometry::PathSet& region);

} // namespace openstitch::stitch_generation
