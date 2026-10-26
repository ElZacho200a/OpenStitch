// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Compensation du tirage AUTOMATIQUE (HP-ENG-001.b, `PullMode::Auto`) : le moteur calcule la
// compensation d'un objet selon la largeur de sa forme, au lieu de laisser l'utilisateur la
// régler à la main. Fonctions pures et déterministes. Les coefficients sont des règles
// empiriques (l'étirement dépend du tissu, de la sous-couche et de la tension : aucun modèle
// physique n'est publié) ; ils sont consignés dans `docs/source/moteur-de-points.md`
// (« Compensation du tirage automatique ») et à recaler par la pièce-test HP-ENG-001.a.

// Remplissage (tatami, directionnel) : dépassement de bout de rangée dans l'axe du fil.
// `0,15 mm + 0,03 × épaisseur moyenne`, borné à [0,15 ; 0,40] mm : une rangée plus longue
// tire davantage, mais l'effet sature. Épaisseur nulle ou négative : 0 (rien à compenser).
[[nodiscard]] Micrometers choose_fill_pull(double mean_width_mm);

// Satin : élargissement de CHAQUE côté de la colonne (comme `SatinParams::pull_compensation`).
// `5 % de la largeur`, borné à [0,10 ; 0,35] mm. Largeur nulle ou négative : 0.
[[nodiscard]] Micrometers choose_satin_pull(double width_mm);

// Retrait de bord EFFECTIF d'un remplissage. `Manual` : le retrait de l'objet. `Auto` : 0.
// Le retrait de 0,2 mm par défaut compense le gonflement du bord, mais il laisse ~0,4 mm de vide
// entre deux zones voisines (filet de tissu visible) ; en mode Auto, la compensation du tirage
// le remplace : la forme cousue part du contour vectoriel, ses rangées le dépassent de
// `choose_fill_pull` dans l'axe du fil. Sans quoi les deux compensations s'additionneraient.
[[nodiscard]] Micrometers effective_fill_inset(document::PullMode mode, Micrometers inset);

// Paramètres EFFECTIFS d'un remplissage pour une région : copie inchangée en mode `Manual`,
// compensation choisie par `choose_fill_pull` (épaisseur moyenne de la région) en mode `Auto`.
[[nodiscard]] document::TatamiParams resolve_pull_compensation(const document::TatamiParams& params,
                                                               const geometry::PathSet& region);
[[nodiscard]] document::DirectionalFillParams
resolve_pull_compensation(const document::DirectionalFillParams& params,
                          const geometry::PathSet& region);

} // namespace openstitch::stitch_generation
