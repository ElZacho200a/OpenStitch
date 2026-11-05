// SPDX-License-Identifier: Apache-2.0
#include "segmentation_preview.hpp"

#include <algorithm>
#include <chrono>

#include "openstitch/image/image.hpp"

namespace openstitch::desktop_v2 {

Result<SegmentationPreview> previewSegmentation(const image::Image& img,
                                                const segmentation::SegmentationOptions& options,
                                                const TaskContext& task) {
    if (task.cancelled()) {
        return cancelled_error();
    }
    const auto start = std::chrono::steady_clock::now();
    auto segmented = segmentation::segment(img, options);
    if (!segmented) {
        return std::unexpected(segmented.error());
    }
    if (task.cancelled()) {
        return cancelled_error(); // résultat périmé : jamais livré
    }
    SegmentationPreview out;
    out.options = options;
    out.width = segmented->width;
    out.height = segmented->height;
    const auto map = segmentation::render_map(*segmented);
    out.rgba = map.rgba;
    // Régions vivantes, les plus grandes d'abord (ordre stable : à taille égale, par identifiant).
    std::vector<const segmentation::Region*> regions;
    for (const auto& slot : segmented->region_slots) {
        if (slot) {
            regions.push_back(&*slot);
        }
    }
    std::stable_sort(regions.begin(), regions.end(),
                     [](const auto* a, const auto* b) { return a->pixel_count > b->pixel_count; });
    out.regions = regions.size();
    for (const auto* region : regions) {
        out.palette.push_back(region->rgb);
        out.smallestRegionPx = out.smallestRegionPx == 0
                                   ? region->pixel_count
                                   : std::min(out.smallestRegionPx, region->pixel_count);
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}

Result<SegmentationPreview> previewSegmentation(const std::filesystem::path& image,
                                                const segmentation::SegmentationOptions& options,
                                                const TaskContext& task) {
    auto loaded = image::load_image(image);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    return previewSegmentation(*loaded, options, task);
}

} // namespace openstitch::desktop_v2
