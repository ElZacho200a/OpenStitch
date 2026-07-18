// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/image/ops.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::document {

// État du document : une image source intacte, une résolution de travail,
// une pile de transformations rejouables et l'éventuelle segmentation de
// l'image de travail. L'image de travail est TOUJOURS recalculée
// (image::apply_pipeline), jamais stockée comme vérité.
// Ce type grandira (calques, objets, palette…) au fil des phases.
struct Project {
    image::Image original;                    // jamais modifiée après l'import
    Millimeters mm_per_px{25.4 / 96.0};       // résolution de travail
    std::vector<image::ImageOp> ops;          // pile de prétraitements

    // Segmentation de l'image de travail. Invalidée (remise à nullopt) par
    // toute nouvelle opération de prétraitement.
    std::optional<segmentation::Segmentation> segmentation;

    [[nodiscard]] bool hasImage() const { return !original.empty(); }
};

}  // namespace openstitch::document
