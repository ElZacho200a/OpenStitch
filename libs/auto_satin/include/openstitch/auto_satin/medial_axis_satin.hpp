// SPDX-License-Identifier: Apache-2.0
// Auto-satin par axe médian vectoriel (docs/auto-satin-research.md, lot B). Même moteur de
// traversées, cellules, coudes, guides et finitions que le squelette raster ; seul l'AXE change :
// il vient du diagramme de Voronoï des segments du contour (Boost.Polygon, BSL-1.0, encapsulé
// dans `medial_axis.cpp`) au lieu d'un amincissement de pixels. Plus de discrétisation de l'axe
// par pixels ; la sensibilité de l'axe médian au bruit du contour demeure (même élagage à erreur
// bornée que le squelette raster).
#pragma once

#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

// Déterministe. Refuse (erreur nommée) un contour qui s'auto-intersecte ou qui est trop complexe
// pour la méthode : aucun repli silencieux sur une autre méthode. Utilise `analysis.cleanup`,
// `spacing`, `compact_axis`, les guides et les autres champs du moteur à axe.
[[nodiscard]] Result<SkeletonSatinResult>
generate_medial_axis_satin(const geometry::PathSet& region, const SkeletonSatinParameters& params);

} // namespace openstitch::auto_satin
