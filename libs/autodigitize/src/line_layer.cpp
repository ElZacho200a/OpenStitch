// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/line_layer.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace openstitch::autodigitize {

namespace {

// Pixels de `mask` dont le voisinage (fenêtre de rayon `radius`, pixels hors `mask`) contient au
// moins deux régions de couleurs différentes : séparations entre deux zones, pas des objets.
// Un voisin « fond » (label 0) ne compte pas : le contour du motif reste un trait.
std::vector<char> separation_mask(const segmentation::Segmentation& seg,
                                  const std::vector<char>& mask, int radius) {
    const int w = seg.width;
    const int h = seg.height;
    std::vector<std::uint32_t> colour(seg.region_slots.size() + 1, 0);
    for (std::size_t i = 0; i < seg.region_slots.size(); ++i) {
        if (seg.region_slots[i]) {
            const auto& c = seg.region_slots[i]->rgb;
            colour[i + 1] =
                0x1000000u | (std::uint32_t{c[0]} << 16) | (std::uint32_t{c[1]} << 8) | c[2];
        }
    }
    std::vector<char> sep(mask.size(), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * w + x;
            if (mask[idx] == 0) {
                continue;
            }
            std::uint32_t first = 0;
            bool found = false;
            for (int dy = -radius; dy <= radius && !found; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h) {
                    continue;
                }
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int xx = x + dx;
                    if (xx < 0 || xx >= w) {
                        continue;
                    }
                    const std::size_t j = static_cast<std::size_t>(yy) * w + xx;
                    const std::uint32_t label = seg.labels[j];
                    if (mask[j] != 0 || label == 0 || label >= colour.size() ||
                        colour[label] == 0) {
                        continue;
                    }
                    if (first == 0) {
                        first = colour[label];
                    } else if (colour[label] != first) {
                        found = true;
                        break;
                    }
                }
            }
            sep[idx] = found ? 1 : 0;
        }
    }
    return sep;
}

// Fermeture morphologique (dilatation puis érosion, carré de rayon `r`) de `m`, limitée à `limit` :
// comble les coupures d'un trait qui traverse une frontière entre deux zones.
std::vector<char> close_within(const std::vector<char>& m, const std::vector<char>& limit, int w,
                               int h, int r) {
    const auto pass = [&](const std::vector<char>& in, bool dilate) {
        std::vector<char> out(in.size(), dilate ? 0 : 1);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                bool any = false;
                bool all = true;
                for (int dy = -r; dy <= r; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        const int yy = y + dy;
                        const int xx = x + dx;
                        const bool v = yy >= 0 && yy < h && xx >= 0 && xx < w &&
                                       in[static_cast<std::size_t>(yy) * w + xx] != 0;
                        any = any || v;
                        all = all && v;
                    }
                }
                out[static_cast<std::size_t>(y) * w + x] = (dilate ? any : all) ? 1 : 0;
            }
        }
        return out;
    };
    std::vector<char> closed = pass(pass(m, true), false);
    for (std::size_t i = 0; i < closed.size(); ++i) {
        closed[i] = (closed[i] != 0 && limit[i] != 0) ? 1 : 0;
    }
    return closed;
}

} // namespace

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

    // Pixels réellement cousus en lignes. Par défaut tous les pixels de trait ; avec
    // `objects_only`, les séparations entre deux zones sont exclues (elles restent retirées des
    // remplissages, rendues aux zones voisines, mais ne sont pas cousues).
    std::vector<char> stitchMask = mask;
    std::size_t separationPixels = 0;
    if (lines.objects_only) {
        const int radius = std::max(2, diameter / 2 + 1);
        const std::vector<char> sep = separation_mask(seg, mask, radius);
        std::vector<char> kept(mask.size(), 0);
        for (std::size_t i = 0; i < mask.size(); ++i) {
            kept[i] = (mask[i] != 0 && sep[i] == 0) ? 1 : 0;
        }
        stitchMask = close_within(kept, mask, seg.width, seg.height, 2);
        separationPixels =
            linePixels -
            static_cast<std::size_t>(std::count(stitchMask.begin(), stitchMask.end(), char{1}));
    }
    const std::size_t stitchPixels =
        static_cast<std::size_t>(std::count(stitchMask.begin(), stitchMask.end(), char{1}));

    // La couleur moyenne des traits est presque toujours à quelques unités d'un noir déjà
    // présent dans la palette (mesuré : #0C121C contre #08080F) : un fil de plus pour rien.
    // On reprend alors la couleur de la région existante la plus proche (déterministe : plus
    // petite distance, premier slot en cas d'égalité).
    {
        constexpr int kSnapMaxDistSq = 30 * 30; // distance RGB euclidienne <= 30
        int best = kSnapMaxDistSq + 1;
        for (const auto& slot : seg.region_slots) {
            if (!slot) {
                continue;
            }
            int d = 0;
            for (std::size_t c = 0; c < 3; ++c) {
                const int diff = int{slot->rgb[c]} - int{lineRgb[c]};
                d += diff * diff;
            }
            if (d < best) {
                best = d;
                lineRgb = slot->rgb;
            }
        }
    }

    // Segmentation des traits : une seule région (la couleur moyenne des pixels de trait).
    segmentation::Segmentation lineSeg;
    lineSeg.width = seg.width;
    lineSeg.height = seg.height;
    lineSeg.labels.assign(seg.labels.size(), 0);
    segmentation::Region lineRegion;
    lineRegion.id = RegionId{1};
    lineRegion.rgb = lineRgb;
    lineRegion.pixel_count = stitchPixels;
    lineSeg.region_slots.push_back(lineRegion);
    for (std::size_t i = 0; i < stitchMask.size(); ++i) {
        if (stitchMask[i] != 0) {
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
    if (stitchPixels == 0) {
        // Tous les traits étaient des séparations : rien à coudre par-dessus.
        if (metrics != nullptr) {
            metrics->line_pixels = 0;
            metrics->reassigned_pixels = reassigned;
            metrics->separation_pixels = separationPixels;
        }
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
    // Les remplissages de la couleur des traits passent en dernier (ordre relatif conservé) :
    // les traits s'enchaînent alors sur le même fil, sans changement de fil supplémentaire.
    std::stable_partition(out.embroideries.begin(), out.embroideries.end(),
                          [&](const document::EmbroideryObject& e) { return e.rgb != lineRgb; });
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
        metrics->line_pixels = stitchPixels;
        metrics->separation_pixels = separationPixels;
        metrics->reassigned_pixels = reassigned;
        metrics->contours = cm;
        metrics->line_objects = out.embroideries.size() - fillObjects;
    }
    return out;
}

} // namespace openstitch::autodigitize
