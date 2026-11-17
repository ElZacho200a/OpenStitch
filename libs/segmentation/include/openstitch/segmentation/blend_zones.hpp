// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::segmentation {

// ZONES DE FONDU (2026-11-17, docs/fondus-picturaux-autodigitize.md §2 et lot 2). Une peinture
// (ciel de Turner, eau de Monet) se découpe en zones où la couleur varie LENTEMENT ; seul un
// bord net sépare deux zones. Contrairement à `segment` (quantification en N couleurs, qui
// découpe chaque dégradé en bandes et en îlots), une zone garde tout son dégradé : elle sera
// cousue en fondu à parts lues dans l'image.
struct BlendZoneOptions {
    // Résolution de travail : l'image est réduite par moyenne à ce pas (jamais agrandie).
    double work_mm_per_px{0.5};
    // Flou gaussien (écart type, mm) : couleurs « vues de loin », sans le grain de la toile.
    double smoothing_mm{0.75};
    // Force de bord (ΔE Lab par pas de 0,5 mm, Sobel normalisé) en deçà de laquelle deux zones
    // voisines fusionnent. 2,5 : réglage de l'étude (27 zones sur Le Téméraire).
    double edge_threshold{2.5};
    // Zones plus petites absorbées par la voisine au plus long bord commun.
    double min_zone_mm2{25.0};
    // Écart des germes du partage des eaux (pixels de travail ; ~1 germe pour 400 px).
    int seed_spacing_px{20};
};

// Partage des eaux sur le gradient Lab de l'image lissée, puis fusion hiérarchique selon la
// FORCE DU BORD commun (moyenne du gradient le long du bord, pondérée par sa longueur) et non
// selon l'écart des couleurs moyennes : un dégradé lent se fond en une seule zone, un bord net
// l'arrête. Étiquettes ramenées à la taille de `img` ; pixels transparents (alpha < 128) hors
// zone. Couleur d'une région = moyenne sRVB de ses pixels. Zones connexes. Déterministe, sans
// bibliothèque tierce. Erreur utilisateur si l'image est vide ou entièrement transparente.
[[nodiscard]] Result<Segmentation> segment_blend_zones(const image::Image& img,
                                                       Millimeters mm_per_px,
                                                       const BlendZoneOptions& options = {});

} // namespace openstitch::segmentation
