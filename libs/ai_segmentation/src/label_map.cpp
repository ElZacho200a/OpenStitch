// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/label_map.hpp"

#include <algorithm>
#include <numeric>

namespace openstitch::ai_segmentation {

namespace {

std::size_t count_nonzero(const std::vector<std::uint8_t>& pixels) {
    return static_cast<std::size_t>(std::count_if(
        pixels.begin(), pixels.end(), [](std::uint8_t v) { return v != 0; }));
}

}  // namespace

Result<segmentation::Segmentation> build_label_map(const std::vector<LabelMaskInput>& masks,
                                                    const LabelMapOptions& options) {
    if (options.width <= 0 || options.height <= 0) {
        return fail(ErrorCategory::Internal, "Dimensions de carte de labels invalides");
    }
    if (masks.empty()) {
        return fail(ErrorCategory::UserInput,
                    "Aucun masque retenu pour la construction de la carte de labels");
    }
    const auto pixelCount =
        static_cast<std::size_t>(options.width) * static_cast<std::size_t>(options.height);
    for (const auto& mask : masks) {
        if (mask.pixels.size() != pixelCount) {
            return fail(ErrorCategory::Internal,
                        "Masque de taille incohérente avec la carte de labels",
                        "mask_id=" + std::to_string(mask.mask_id));
        }
    }

    std::vector<std::size_t> areas(masks.size());
    for (std::size_t i = 0; i < masks.size(); ++i) {
        areas[i] = count_nonzero(masks[i].pixels);
    }

    // Ordre de résolution des conflits, du plus prioritaire au moins
    // prioritaire (cf. commentaire du header). Le dernier critère (index
    // d'origine) rend le tri totalement déterministe quel que soit
    // l'algorithme de tri utilisé.
    std::vector<std::size_t> priorityOrder(masks.size());
    std::iota(priorityOrder.begin(), priorityOrder.end(), std::size_t{0});
    std::sort(priorityOrder.begin(), priorityOrder.end(), [&](std::size_t a, std::size_t b) {
        const LabelMaskInput& ma = masks[a];
        const LabelMaskInput& mb = masks[b];
        if (ma.manual_priority_set != mb.manual_priority_set) {
            return ma.manual_priority_set;  // priorité manuelle passe devant
        }
        if (ma.manual_priority_set) {
            if (ma.manual_priority != mb.manual_priority) {
                return ma.manual_priority < mb.manual_priority;
            }
        } else {
            if (ma.stability_score != mb.stability_score) {
                return ma.stability_score > mb.stability_score;
            }
            if (ma.predicted_iou != mb.predicted_iou) {
                return ma.predicted_iou > mb.predicted_iou;
            }
            if (areas[a] != areas[b]) {
                return areas[a] > areas[b];
            }
        }
        return a < b;
    });

    segmentation::Segmentation seg;
    seg.width = options.width;
    seg.height = options.height;
    seg.labels.assign(pixelCount, 0);
    seg.region_slots.reserve(masks.size());
    for (const auto& mask : masks) {
        segmentation::Region region;
        region.id = RegionId{seg.region_slots.size() + 1};
        region.rgb = mask.rgb;
        seg.region_slots.push_back(region);
    }

    for (std::size_t p = 0; p < pixelCount; ++p) {
        for (const std::size_t idx : priorityOrder) {
            if (masks[idx].pixels[p] != 0) {
                seg.labels[p] = static_cast<std::uint32_t>(idx + 1);
                ++seg.region_slots[idx]->pixel_count;
                break;
            }
        }
    }

    return seg;
}

}  // namespace openstitch::ai_segmentation
