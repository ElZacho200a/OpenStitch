// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/color_refine.hpp"

#include <algorithm>
#include <vector>

namespace openstitch::ai_segmentation {

Result<segmentation::Segmentation> refine_label_map_by_color(
    const segmentation::Segmentation& labelMap, const image::Image& sourceImage,
    const ColorRefineOptions& options) {
    if (labelMap.width != sourceImage.width || labelMap.height != sourceImage.height) {
        return fail(ErrorCategory::Internal,
                    "Dimensions incohérentes entre la carte de labels et l'image source");
    }

    segmentation::Segmentation result;
    result.width = labelMap.width;
    result.height = labelMap.height;
    result.labels.assign(labelMap.labels.size(), 0);

    const segmentation::SegmentationOptions segOptions{options.max_colors, options.min_region_px};

    for (std::size_t slot = 0; slot < labelMap.region_slots.size(); ++slot) {
        if (!labelMap.region_slots[slot]) {
            continue;
        }
        const auto regionLabel = static_cast<std::uint32_t>(slot + 1);

        // Sous-image : pixels hors de la région -> alpha 0 (fond, ignoré par
        // segment(), exactement comme pour une image importée partiellement
        // transparente).
        image::Image masked;
        masked.width = sourceImage.width;
        masked.height = sourceImage.height;
        masked.source_had_alpha = true;
        masked.rgba.assign(sourceImage.rgba.size(), 0);
        bool anyPixel = false;
        for (std::size_t p = 0; p < labelMap.labels.size(); ++p) {
            if (labelMap.labels[p] == regionLabel) {
                std::copy_n(sourceImage.rgba.data() + p * 4, 4, masked.rgba.data() + p * 4);
                masked.rgba[p * 4 + 3] = 255;  // opaque à l'intérieur de la région, quoi qu'il arrive
                anyPixel = true;
            }
        }
        if (!anyPixel) {
            continue;
        }

        const auto subSeg = segmentation::segment(masked, segOptions);
        if (!subSeg) {
            continue;  // région trop petite/uniforme pour être quantifiée : ignorée sans erreur
        }

        // Fusionne les sous-régions dans le résultat global, avec de nouveaux ids.
        std::vector<std::uint32_t> localToGlobal(subSeg->region_slots.size() + 1, 0);
        for (std::size_t i = 0; i < subSeg->region_slots.size(); ++i) {
            if (!subSeg->region_slots[i]) {
                continue;
            }
            segmentation::Region region = *subSeg->region_slots[i];
            region.id = RegionId{result.region_slots.size() + 1};
            localToGlobal[i + 1] = static_cast<std::uint32_t>(result.region_slots.size() + 1);
            result.region_slots.push_back(region);
        }
        for (std::size_t p = 0; p < subSeg->labels.size(); ++p) {
            const std::uint32_t localLabel = subSeg->labels[p];
            if (localLabel != 0) {
                result.labels[p] = localToGlobal[localLabel];
            }
        }
    }

    if (result.region_slots.empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "Aucune région exploitable après découpage par couleur");
    }
    return result;
}

}  // namespace openstitch::ai_segmentation
