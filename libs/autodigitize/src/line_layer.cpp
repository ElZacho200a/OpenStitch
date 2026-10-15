// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/line_layer.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace openstitch::autodigitize {

Result<AutoResult> auto_digitize_with_lines(const segmentation::Segmentation& seg,
                                            const image::Image& source, IdGenerator<ObjectId>& ids,
                                            const AutoOptions& options,
                                            const LineLayerOptions& lines,
                                            const ContourOptions& contour_options,
                                            LineLayerMetrics* metrics) {
    if (!lines.enabled) {
        return auto_digitize(seg, ids, options);
    }
    // Largeur d'ouverture en pixels : une partie plus étroite que ce diamètre est un trait.
    const double px_mm = std::max(1e-6, options.mm_per_px.value);
    const int diameter =
        std::max(2, static_cast<int>(std::lround(lines.max_line_width_mm / px_mm)));
    std::array<std::uint8_t, 3> lineRgb{10, 10, 20};
    const std::vector<char> mask =
        segmentation::dark_thin_mask(source, lines.dark_max_lightness, diameter, &lineRgb);
    const std::size_t linePixels =
        static_cast<std::size_t>(std::count(mask.begin(), mask.end(), char{1}));
    if (mask.size() != seg.labels.size() || linePixels == 0) {
        return auto_digitize(seg, ids, options); // aucun trait sombre : rien à séparer
    }

    // Segmentation des traits : une seule région (la couleur moyenne des pixels de trait).
    segmentation::Segmentation lineSeg;
    lineSeg.width = seg.width;
    lineSeg.height = seg.height;
    lineSeg.labels.assign(seg.labels.size(), 0);
    segmentation::Region lineRegion;
    lineRegion.id = RegionId{1};
    lineRegion.rgb = lineRgb;
    lineRegion.pixel_count = linePixels;
    lineSeg.region_slots.push_back(lineRegion);
    for (std::size_t i = 0; i < mask.size(); ++i) {
        if (mask[i] != 0) {
            lineSeg.labels[i] = 1;
        }
    }

    // Segmentation des remplissages : les pixels de trait sont rendus aux régions voisines (les
    // remplissages passent dessous). Un pixel sans autre voisine garde son label (trait posé sur un
    // aplat), sauf si sa région est presque entièrement faite de pixels de trait : elle ne produit
    // alors que des lignes et quitte les remplissages.
    segmentation::Segmentation fills = seg;
    std::size_t reassigned =
        segmentation::reassign_to_neighbours(fills, mask, /*leftover_to_background=*/false);
    {
        std::vector<std::size_t> total(seg.region_slots.size() + 1, 0);
        std::vector<std::size_t> inMask(seg.region_slots.size() + 1, 0);
        for (std::size_t i = 0; i < seg.labels.size(); ++i) {
            if (seg.labels[i] != 0) {
                ++total[seg.labels[i]];
                inMask[seg.labels[i]] += mask[i] != 0 ? 1 : 0;
            }
        }
        std::vector<char> lineOnly(mask.size(), 0);
        bool any = false;
        for (std::size_t i = 0; i < mask.size(); ++i) {
            const std::uint32_t l = seg.labels[i];
            if (mask[i] != 0 && l != 0 && fills.labels[i] == l && inMask[l] * 10 >= total[l] * 9) {
                lineOnly[i] = 1;
                any = true;
            }
        }
        if (any) {
            reassigned += segmentation::reassign_to_neighbours(fills, std::move(lineOnly),
                                                               /*leftover_to_background=*/true);
        }
    }

    // Un aplat que le trait coupait en deux morceaux (deux régions de même couleur) redevient
    // adjacent : on les refusionne pour qu'il soit cousu en une seule pièce.
    for (int sweep = 0; sweep < 4; ++sweep) {
        bool merged = false;
        for (const auto& border : segmentation::region_adjacency(fills)) {
            const segmentation::Region* ra = fills.find(border.a);
            const segmentation::Region* rb = fills.find(border.b);
            if (ra != nullptr && rb != nullptr && ra->rgb == rb->rgb &&
                segmentation::merge_regions(fills, border.a, border.b)) {
                merged = true;
            }
        }
        if (!merged) {
            break;
        }
    }

    auto fillResult = auto_digitize(fills, ids, options);
    if (!fillResult) {
        return fillResult;
    }
    ContourOptions co = contour_options;
    co.mm_per_px = options.mm_per_px;
    co.skip_largest_region = false;
    ContourMetrics cm;
    auto lineResult = auto_digitize_contours(lineSeg, ids, co, &cm);
    if (!lineResult) {
        return lineResult;
    }
    AutoResult out = std::move(*fillResult);
    const std::size_t fillObjects = out.embroideries.size();
    for (auto& v : lineResult->vectors) {
        out.vectors.push_back(std::move(v));
    }
    for (auto& e : lineResult->embroideries) {
        out.embroideries.push_back(std::move(e)); // à la suite : cousus après les remplissages
    }
    for (auto& w : lineResult->warnings) {
        out.warnings.push_back(std::move(w));
    }
    if (metrics != nullptr) {
        metrics->line_pixels = linePixels;
        metrics->reassigned_pixels = reassigned;
        metrics->contours = cm;
        metrics->line_objects = out.embroideries.size() - fillObjects;
    }
    return out;
}

} // namespace openstitch::autodigitize
