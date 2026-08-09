// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/core/error.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::ai_segmentation {

// Mêmes bornes que la segmentation classique (`segmentation::SegmentationOptions`) —
// ce n'est pas un second algorithme, seulement le même appliqué région par
// région.
struct ColorRefineOptions {
    int max_colors{8};
    int min_region_px{16};
};

// SAM 2 découpe par FORME/OBJET, jamais par couleur (ce n'est pas un défaut
// de réglage : ce n'est simplement pas ce que fait un modèle de
// segmentation d'objets). Pour l'usage réel visé ici — préparer une image en
// blocs de couleur pour la numérisation — cette fonction reprend chaque
// région de `labelMap` et la subdivise avec l'algorithme de quantification
// CIELAB existant (`segmentation::segment`, le même que le menu
// Segmentation classique), au lieu d'inventer un second algorithme. Une
// région dont la quantification échoue (trop petite/uniforme) est
// silencieusement ignorée, comme pour la segmentation classique. Les pixels
// hors de toute région de `labelMap` restent fond (label 0).
[[nodiscard]] Result<segmentation::Segmentation> refine_label_map_by_color(
    const segmentation::Segmentation& labelMap, const image::Image& sourceImage,
    const ColorRefineOptions& options);

}  // namespace openstitch::ai_segmentation
