// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/line_layer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

#include "openstitch/core/parallel.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "ring_detect.hpp"

namespace openstitch::autodigitize {

namespace {

// Clarté CIELAB L* d'une couleur sRGB (D65).
double cielab_lightness(const std::array<std::uint8_t, 3>& rgb) {
    const auto lin = [](double v) {
        v /= 255.0;
        return v > 0.04045 ? std::pow((v + 0.055) / 1.055, 2.4) : v / 12.92;
    };
    const double y = 0.2126 * lin(rgb[0]) + 0.7152 * lin(rgb[1]) + 0.0722 * lin(rgb[2]);
    const double f = y > 0.008856 ? std::cbrt(y) : 7.787 * y + 16.0 / 116.0;
    return 116.0 * f - 16.0;
}

// Pixels de `mask` dont le voisinage (fenêtre de rayon `radius`, pixels hors `mask`) contient au
// moins deux régions de couleurs différentes : séparations entre deux zones, pas des objets.
// Un voisin « fond » (label 0) ne compte pas : le contour du motif reste un trait.
//
// Les régions de la couleur des traits eux-mêmes (clarté <= `line_lightness`) ne comptent pas comme
// zone voisine : les pixels anti-crénelés d'un trait y sont étiquetés, si bien qu'un hauban sur le
// ciel aurait vu « ciel + noir » et passé pour une séparation (mesuré : presque tous les cordages
// d'un navire étaient écartés).
std::vector<char> separation_mask(const segmentation::Segmentation& seg,
                                  const std::vector<char>& mask, int radius,
                                  double line_lightness) {
    const int w = seg.width;
    const int h = seg.height;
    std::vector<std::uint32_t> colour(seg.region_slots.size() + 1, 0);
    for (std::size_t i = 0; i < seg.region_slots.size(); ++i) {
        if (seg.region_slots[i]) {
            const auto& c = seg.region_slots[i]->rgb;
            if (cielab_lightness(c) <= line_lightness + 1.0) {
                continue; // la couleur des traits eux-mêmes : pas une zone voisine
            }
            colour[i + 1] =
                0x1000000u | (std::uint32_t{c[0]} << 16) | (std::uint32_t{c[1]} << 8) | c[2];
        }
    }
    // HP-PERF-025 : une tâche par ligne (lit seg/mask/colour constants, écrit ses seuls octets).
    std::vector<char> sep(mask.size(), 0);
    parallel_for(static_cast<std::size_t>(h), [&](std::size_t row) {
        const int y = static_cast<int>(row);
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
    });
    return sep;
}

// Retire de `m` les composantes 8-connexes dont la diagonale de boîte englobante est inférieure à
// `min_diag_px`. Renvoie le nombre de pixels retirés. Déterministe (parcours en ordre de lecture).
std::size_t drop_short_components(std::vector<char>& m, int w, int h, double min_diag_px) {
    std::size_t dropped = 0;
    std::vector<char> seen(m.size(), 0);
    std::vector<std::size_t> stack;
    std::vector<std::size_t> comp;
    for (int y0 = 0; y0 < h; ++y0) {
        for (int x0 = 0; x0 < w; ++x0) {
            const std::size_t start = static_cast<std::size_t>(y0) * w + x0;
            if (m[start] == 0 || seen[start] != 0) {
                continue;
            }
            comp.clear();
            stack.assign(1, start);
            seen[start] = 1;
            int minX = x0, maxX = x0, minY = y0, maxY = y0;
            while (!stack.empty()) {
                const std::size_t cur = stack.back();
                stack.pop_back();
                comp.push_back(cur);
                const int cx = static_cast<int>(cur % static_cast<std::size_t>(w));
                const int cy = static_cast<int>(cur / static_cast<std::size_t>(w));
                minX = std::min(minX, cx);
                maxX = std::max(maxX, cx);
                minY = std::min(minY, cy);
                maxY = std::max(maxY, cy);
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = cx + dx;
                        const int ny = cy + dy;
                        if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                            continue;
                        }
                        const std::size_t ni = static_cast<std::size_t>(ny) * w + nx;
                        if (m[ni] != 0 && seen[ni] == 0) {
                            seen[ni] = 1;
                            stack.push_back(ni);
                        }
                    }
                }
            }
            const double diag = std::hypot(static_cast<double>(maxX - minX + 1),
                                           static_cast<double>(maxY - minY + 1));
            if (diag < min_diag_px) {
                for (const std::size_t i : comp) {
                    m[i] = 0;
                }
                dropped += comp.size();
            }
        }
    }
    return dropped;
}

// Fermeture morphologique (dilatation puis érosion, carré de rayon `r`) de `m`, limitée à `limit` :
// comble les coupures d'un trait qui traverse une frontière entre deux zones.
std::vector<char> close_within(const std::vector<char>& m, const std::vector<char>& limit, int w,
                               int h, int r) {
    const auto pass = [&](const std::vector<char>& in, bool dilate) {
        std::vector<char> out(in.size(), dilate ? 0 : 1);
        parallel_for(static_cast<std::size_t>(h), [&](std::size_t row) {
            const int y = static_cast<int>(row);
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
        });
        return out;
    };
    std::vector<char> closed = pass(pass(m, true), false);
    for (std::size_t i = 0; i < closed.size(); ++i) {
        closed[i] = (closed[i] != 0 && limit[i] != 0) ? 1 : 0;
    }
    return closed;
}

// Un trait est LONG devant sa largeur. La pointe effilée d'un aplat (bout de nuage, reflet en
// fuseau) est étroite et claire des deux côtés, mais trapue : diagonale² / pixels (≈ longueur /
// largeur d'une bande) faible. Un réseau (rambarde, gréement) forme une seule composante très
// allongée. Retire de `m` les composantes 8-connexes sous `min_elongation`.
constexpr double kMinElongation = 6.0;

void drop_stubby_components(std::vector<char>& m, int w, int h, double min_elongation) {
    std::vector<char> seen(m.size(), 0);
    std::vector<std::size_t> stack;
    std::vector<std::size_t> comp;
    for (std::size_t start = 0; start < m.size(); ++start) {
        if (m[start] == 0 || seen[start] != 0) {
            continue;
        }
        comp.clear();
        stack.assign(1, start);
        seen[start] = 1;
        int minX = w, maxX = -1, minY = h, maxY = -1;
        while (!stack.empty()) {
            const std::size_t cur = stack.back();
            stack.pop_back();
            comp.push_back(cur);
            const int cx = static_cast<int>(cur % static_cast<std::size_t>(w));
            const int cy = static_cast<int>(cur / static_cast<std::size_t>(w));
            minX = std::min(minX, cx);
            maxX = std::max(maxX, cx);
            minY = std::min(minY, cy);
            maxY = std::max(maxY, cy);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = cx + dx;
                    const int ny = cy + dy;
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                        continue;
                    }
                    const std::size_t ni = static_cast<std::size_t>(ny) * w + nx;
                    if (m[ni] != 0 && seen[ni] == 0) {
                        seen[ni] = 1;
                        stack.push_back(ni);
                    }
                }
            }
        }
        const double dx = maxX - minX + 1;
        const double dy = maxY - minY + 1;
        if ((dx * dx + dy * dy) / static_cast<double>(comp.size()) < min_elongation) {
            for (const std::size_t i : comp) {
                m[i] = 0;
            }
        }
    }
}

// Traits par contraste local : chaque trait garde SA couleur (barreaux marine, treillis brun,
// haubans noirs). Composantes 8-connexes en ordre de lecture ; couleur = moyenne de la moitié la
// plus sombre de ses pixels (le cœur du trait, pas son anti-crénelage), calée sur la couleur de
// région la plus proche de la palette (aucun fil nouveau). Une région par couleur, numérotée dans
// l'ordre de première apparition. Déterministe.
segmentation::Segmentation colour_strokes(const std::vector<char>& stitch,
                                          const image::Image& source,
                                          const segmentation::Segmentation& seg) {
    const int w = seg.width;
    const int h = seg.height;
    segmentation::Segmentation out;
    out.width = w;
    out.height = h;
    out.labels.assign(stitch.size(), 0);
    std::vector<char> seen(stitch.size(), 0);
    std::vector<std::size_t> stack;
    std::vector<std::size_t> comp;
    std::vector<std::pair<double, std::size_t>> byLight;
    for (std::size_t start = 0; start < stitch.size(); ++start) {
        if (stitch[start] == 0 || seen[start] != 0) {
            continue;
        }
        comp.clear();
        stack.assign(1, start);
        seen[start] = 1;
        while (!stack.empty()) {
            const std::size_t cur = stack.back();
            stack.pop_back();
            comp.push_back(cur);
            const int cx = static_cast<int>(cur % static_cast<std::size_t>(w));
            const int cy = static_cast<int>(cur / static_cast<std::size_t>(w));
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = cx + dx;
                    const int ny = cy + dy;
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                        continue;
                    }
                    const std::size_t ni = static_cast<std::size_t>(ny) * w + nx;
                    if (stitch[ni] != 0 && seen[ni] == 0) {
                        seen[ni] = 1;
                        stack.push_back(ni);
                    }
                }
            }
        }
        byLight.clear();
        for (const std::size_t i : comp) {
            const std::uint8_t* px = &source.rgba[i * 4];
            byLight.emplace_back(cielab_lightness({px[0], px[1], px[2]}), i);
        }
        std::sort(byLight.begin(), byLight.end());
        const std::size_t core = std::max<std::size_t>(1, byLight.size() / 2);
        std::uint64_t sum[3] = {0, 0, 0};
        for (std::size_t k = 0; k < core; ++k) {
            const std::uint8_t* px = &source.rgba[byLight[k].second * 4];
            sum[0] += px[0];
            sum[1] += px[1];
            sum[2] += px[2];
        }
        std::array<std::uint8_t, 3> rgb{static_cast<std::uint8_t>(sum[0] / core),
                                        static_cast<std::uint8_t>(sum[1] / core),
                                        static_cast<std::uint8_t>(sum[2] / core)};
        int best = std::numeric_limits<int>::max();
        std::array<std::uint8_t, 3> snapped = rgb;
        for (const auto& slot : seg.region_slots) {
            if (!slot) {
                continue;
            }
            int d = 0;
            for (std::size_t c = 0; c < 3; ++c) {
                const int diff = int{slot->rgb[c]} - int{rgb[c]};
                d += diff * diff;
            }
            if (d < best) {
                best = d;
                snapped = slot->rgb;
            }
        }
        std::uint32_t label = 0;
        for (const auto& slot : out.region_slots) {
            if (slot && slot->rgb == snapped) {
                label = static_cast<std::uint32_t>(slot->id.value);
                break;
            }
        }
        if (label == 0) {
            segmentation::Region r;
            label = static_cast<std::uint32_t>(out.region_slots.size() + 1);
            r.id = RegionId{label};
            r.rgb = snapped;
            out.region_slots.push_back(r);
        }
        auto& region = *out.region_slots[label - 1];
        region.pixel_count += comp.size();
        for (const std::size_t i : comp) {
            out.labels[i] = label;
        }
    }
    return out;
}

} // namespace

LineworkPlan prepare_linework(const segmentation::Segmentation& seg, const image::Image& source,
                              Millimeters mm_per_px, const LineLayerOptions& lines) {
    LineworkPlan plan;
    if (!lines.enabled) {
        plan.completed_fills = seg;
        return plan;
    }
    // Largeur d'ouverture en pixels : une partie plus étroite que ce diamètre est un trait.
    const double px_mm = std::max(1e-6, mm_per_px.value);
    const int diameter =
        std::max(2, static_cast<int>(std::lround(lines.max_line_width_mm / px_mm)));
    std::array<std::uint8_t, 3> lineRgb{10, 10, 20};
    // Seuil de clarté des traits : la couleur la plus SOMBRE réellement présente (le noir des
    // traits) plus une marge, plafonné par `dark_max_lightness`. Le plafond seul (L* <= 25) englobe
    // aussi le bleu marine (L* ~ 16) : ses parties étroites passaient pour des traits, étaient
    // retirées du remplissage marine (trou : seul un trait fin les recouvrait) puis cousues en
    // noir.
    double lineLightness = lines.dark_max_lightness;
    {
        std::size_t labelled = 0;
        for (const std::uint32_t l : seg.labels) {
            labelled += l != 0 ? 1 : 0;
        }
        const std::size_t minPixels = std::max<std::size_t>(30, labelled / 2000);
        double darkest = lines.dark_max_lightness;
        for (const auto& slot : seg.region_slots) {
            if (slot && slot->pixel_count >= minPixels) {
                darkest = std::min(darkest, cielab_lightness(slot->rgb));
            }
        }
        lineLightness = std::min(lines.dark_max_lightness, darkest + 8.0);
    }
    std::vector<char> mask;
    if (lines.local_contrast > 0.0) {
        mask = segmentation::contrast_thin_mask(source, diameter, lines.local_contrast);
        drop_stubby_components(mask, seg.width, seg.height, kMinElongation);
        // Zones « traits fins » : seconde détection, renforcée, limitée aux pixels peints.
        if (lines.zone_mask != nullptr && lines.zone_mask->matches(seg.width, seg.height) &&
            lines.zone_mask->any_detail() && lines.zone_contrast > 0.0) {
            const int zoneDiameter =
                std::max(2, static_cast<int>(std::lround(lines.zone_width_mm / px_mm)));
            auto zone = segmentation::contrast_thin_mask(source, zoneDiameter, lines.zone_contrast);
            for (std::size_t i = 0; i < zone.size(); ++i) {
                if (lines.zone_mask->weights[i] < 128) {
                    zone[i] = 0;
                }
            }
            drop_stubby_components(zone, seg.width, seg.height, lines.zone_min_elongation);
            for (std::size_t i = 0; i < zone.size() && i < mask.size(); ++i) {
                mask[i] = static_cast<char>(mask[i] != 0 || zone[i] != 0 ? 1 : 0);
            }
        }
        std::uint64_t sum[3] = {0, 0, 0};
        std::uint64_t count = 0;
        for (std::size_t i = 0; i < mask.size(); ++i) {
            if (mask[i] != 0) {
                for (int c = 0; c < 3; ++c) {
                    sum[c] += source.rgba[i * 4 + static_cast<std::size_t>(c)];
                }
                ++count;
            }
        }
        if (count > 0) {
            lineRgb = {static_cast<std::uint8_t>(sum[0] / count),
                       static_cast<std::uint8_t>(sum[1] / count),
                       static_cast<std::uint8_t>(sum[2] / count)};
            lineLightness = cielab_lightness(lineRgb);
        }
    } else {
        mask = segmentation::dark_thin_mask(source, lineLightness, diameter, &lineRgb);
    }
    const std::size_t linePixels =
        static_cast<std::size_t>(std::count(mask.begin(), mask.end(), char{1}));
    // Barres : parties sombres plus larges qu'un trait mais plus étroites que `bars_max_width_mm`
    // (mât, vergues) : ni trait (un point droit les amincit), ni remplissage (le réseau mât +
    // vergues
    // + coque en un seul bloc ressort hachuré). Seuil SERRÉ : la couleur réelle des traits (noir)
    // plus une marge, car le seuil large (L* <= 25) englobe le bleu marine (L* ~ 16) dont les
    // parties étroites seraient recolorées en noir. Sont exclus ce qui est déjà un trait et ce qui
    // est fin au même seuil serré (contour noir collé à un aplat marine : laissé au remplissage).
    std::vector<char> barsMask;
    std::size_t barsPixels = 0;
    if (lines.bars_max_width_mm > 0.0 && mask.size() == seg.labels.size()) {
        const int barsDiameter =
            std::max(diameter + 2, static_cast<int>(std::lround(lines.bars_max_width_mm / px_mm)));
        barsMask = segmentation::dark_thin_mask(source, lineLightness, barsDiameter);
        if (barsMask.size() == mask.size()) {
            for (std::size_t i = 0; i < mask.size(); ++i) {
                barsMask[i] = (barsMask[i] != 0 && mask[i] == 0) ? 1 : 0;
            }
            barsPixels =
                static_cast<std::size_t>(std::count(barsMask.begin(), barsMask.end(), char{1}));
        } else {
            barsMask.clear();
        }
    }
    if (mask.size() != seg.labels.size() || (linePixels == 0 && barsPixels == 0)) {
        plan.completed_fills = seg; // aucun trait sombre : rien à séparer
        return plan;
    }
    // Pixels à rendre aux zones voisines : traits ET barres.
    std::vector<char> fillMask = mask;
    for (std::size_t i = 0; i < barsMask.size(); ++i) {
        fillMask[i] = (fillMask[i] != 0 || barsMask[i] != 0) ? 1 : 0;
    }

    // Pixels réellement cousus en lignes. Par défaut tous les pixels de trait ; avec
    // `objects_only`, les séparations entre deux zones sont exclues (elles restent retirées des
    // remplissages, rendues aux zones voisines, mais ne sont pas cousues).
    std::vector<char> stitchMask = mask;
    std::vector<char> sepDebug;
    std::size_t separationPixels = 0;
    if (lines.objects_only) {
        const int radius = std::max(2, diameter / 2 + 1);
        const std::vector<char> sep = separation_mask(seg, mask, radius, lineLightness);
        std::vector<char> kept(mask.size(), 0);
        for (std::size_t i = 0; i < mask.size(); ++i) {
            kept[i] = (mask[i] != 0 && sep[i] == 0) ? 1 : 0;
        }
        if (lines.masks != nullptr) {
            sepDebug = sep;
        }
        stitchMask = close_within(kept, mask, seg.width, seg.height, 2);
        separationPixels =
            linePixels -
            static_cast<std::size_t>(std::count(stitchMask.begin(), stitchMask.end(), char{1}));
    }
    // Anneaux : retirés des traits cousus, cousus à part en satin (objets créés en fin de
    // fonction).
    std::vector<detail::RingSpec> rings;
    if (lines.rings_satin) {
        std::vector<char> dark(mask.size(), 0);
        for (std::size_t i = 0; i < dark.size(); ++i) {
            const std::uint8_t* px = &source.rgba[i * 4];
            dark[i] =
                (px[3] >= 128 && cielab_lightness({px[0], px[1], px[2]}) <= lineLightness) ? 1 : 0;
        }
        detail::RingDetectOptions ro;
        ro.min_inner_diameter_px = std::max(4.0, 1.0 / px_mm);
        ro.max_inner_diameter_px = 25.0 / px_mm;
        ro.min_width_px = std::max(2.0, 0.35 / px_mm);
        ro.max_width_px = 4.5 / px_mm;
        rings = detail::detect_rings(dark, seg.width, seg.height, ro);
        std::vector<char> band(mask.size(), 0);
        for (const auto& r : rings) {
            detail::paint_ring_band(r, dark, seg.width, seg.height, band);
        }
        for (std::size_t i = 0; i < stitchMask.size(); ++i) {
            if (band[i] != 0) {
                stitchMask[i] = 0;
            }
        }
    }
    std::size_t shortPixels = 0;
    std::vector<char> beforeShort;
    if (lines.masks != nullptr) {
        beforeShort = stitchMask;
    }
    if (lines.min_line_length_mm > 0.0) {
        shortPixels = drop_short_components(stitchMask, seg.width, seg.height,
                                            lines.min_line_length_mm / px_mm);
    }
    if (lines.masks != nullptr) {
        LineLayerMasks& m = *lines.masks;
        m.width = seg.width;
        m.height = seg.height;
        m.thin = mask;
        m.bars = barsMask;
        m.separation = sepDebug;
        m.stitched = stitchMask;
        m.short_frag.assign(mask.size(), 0);
        for (std::size_t i = 0; i < beforeShort.size(); ++i) {
            m.short_frag[i] = (beforeShort[i] != 0 && stitchMask[i] == 0) ? 1 : 0;
        }
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
    if (lines.local_contrast > 0.0 && stitchPixels > 0) {
        lineSeg = colour_strokes(stitchMask, source, seg);
    }

    // Segmentation des remplissages : les pixels de trait sont rendus aux régions voisines (les
    // remplissages passent dessous). Un pixel sans autre voisine garde son label (trait posé sur un
    // aplat), sauf si sa région est presque entièrement faite de pixels de trait : elle ne produit
    // alors que des lignes et quitte les remplissages.
    segmentation::Segmentation fills = seg;
    std::size_t reassigned =
        segmentation::reassign_to_neighbours(fills, fillMask, /*leftover_to_background=*/false);
    {
        std::vector<std::size_t> total(seg.region_slots.size() + 1, 0);
        std::vector<std::size_t> inMask(seg.region_slots.size() + 1, 0);
        for (std::size_t i = 0; i < seg.labels.size(); ++i) {
            if (seg.labels[i] != 0) {
                ++total[seg.labels[i]];
                inMask[seg.labels[i]] += fillMask[i] != 0 ? 1 : 0;
            }
        }
        std::vector<char> lineOnly(mask.size(), 0);
        bool any = false;
        for (std::size_t i = 0; i < mask.size(); ++i) {
            const std::uint32_t l = seg.labels[i];
            if (fillMask[i] != 0 && l != 0 && fills.labels[i] == l &&
                inMask[l] * 10 >= total[l] * 9) {
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

    // Barres : une région d'une seule couleur.
    if (barsPixels > 0) {
        segmentation::Segmentation& barSeg = plan.bars;
        barSeg.width = seg.width;
        barSeg.height = seg.height;
        barSeg.labels.assign(seg.labels.size(), 0);
        segmentation::Region barRegion;
        barRegion.id = RegionId{1};
        barRegion.rgb = lineRgb;
        barRegion.pixel_count = barsPixels;
        barSeg.region_slots.push_back(barRegion);
        for (std::size_t i = 0; i < barsMask.size(); ++i) {
            if (barsMask[i] != 0) {
                barSeg.labels[i] = 1;
            }
        }
    }
    if (stitchPixels > 0) {
        plan.strokes = std::move(lineSeg);
    }
    for (const auto& ring : rings) {
        plan.rings.push_back(
            LineworkRing{ring.cx, ring.cy, ring.a, ring.b, ring.angle, ring.width});
    }
    plan.has_linework = true;
    plan.completed_fills = std::move(fills);
    plan.line_rgb = lineRgb;
    plan.line_lightness = lineLightness;
    plan.stroke_pixels = stitchPixels;
    plan.bar_pixels = barsPixels;
    plan.reassigned_pixels = reassigned;
    plan.separation_pixels = separationPixels;
    plan.short_fragment_pixels = shortPixels;
    return plan;
}

Result<AutoResult> auto_digitize_with_lines(const segmentation::Segmentation& seg,
                                            const image::Image& source, IdGenerator<ObjectId>& ids,
                                            const AutoOptions& options,
                                            const LineLayerOptions& lines,
                                            const ContourOptions& contour_options,
                                            LineLayerMetrics* metrics) {
    if (!lines.enabled) {
        return auto_digitize(seg, ids, options);
    }
    const LineworkPlan plan = prepare_linework(seg, source, options.mm_per_px, lines);
    if (!plan.has_linework) {
        return auto_digitize(seg, ids, options);
    }
    const double px_mm = std::max(1e-6, options.mm_per_px.value);
    const std::array<std::uint8_t, 3> lineRgb = plan.line_rgb;
    const std::size_t barsPixels = plan.bar_pixels;
    const std::size_t stitchPixels = plan.stroke_pixels;
    const segmentation::Segmentation& fills = plan.completed_fills;
    const segmentation::Segmentation& lineSeg = plan.strokes;
    const std::vector<LineworkRing>& rings = plan.rings;

    auto fillResult = auto_digitize(fills, ids, options);
    if (!fillResult) {
        return fillResult;
    }
    AutoResult out = std::move(*fillResult);
    // Les remplissages de la couleur des traits passent en dernier (ordre relatif conservé) :
    // barres et traits s'enchaînent alors sur le même fil, sans changement de fil supplémentaire.
    std::stable_partition(out.embroideries.begin(), out.embroideries.end(),
                          [&](const document::EmbroideryObject& e) { return e.rgb != lineRgb; });
    const std::size_t fillObjects = out.embroideries.size();
    const auto append = [&out](AutoResult& r) {
        for (auto& v : r.vectors) {
            out.vectors.push_back(std::move(v));
        }
        for (auto& e : r.embroideries) {
            out.embroideries.push_back(std::move(e)); // à la suite : cousus après les remplissages
        }
        for (auto& w : r.warnings) {
            out.warnings.push_back(std::move(w));
        }
    };

    // Barres : une région d'une seule couleur, cousue en bandes (satin si la couverture le permet).
    if (barsPixels > 0) {
        const segmentation::Segmentation& barSeg = plan.bars;
        AutoOptions bo = options;
        bo.satin_bands = true;
        bo.skip_largest_region = false;
        bo.satin_max_width = Micrometers{
            static_cast<std::int32_t>(std::lround(lines.bars_max_width_mm * 1000.0)) + 1000};
        auto barResult = auto_digitize(barSeg, ids, bo);
        if (!barResult) {
            return barResult;
        }
        append(*barResult);
    }
    const std::size_t barObjectsEnd = out.embroideries.size();

    // Traits fins : lignes médianes en point droit.
    ContourMetrics cm;
    if (stitchPixels > 0) {
        ContourOptions co = contour_options;
        co.mm_per_px = options.mm_per_px;
        co.skip_largest_region = false;
        auto lineResult = auto_digitize_contours(lineSeg, ids, co, &cm);
        if (!lineResult) {
            return lineResult;
        }
        append(*lineResult);
    }
    // Anneaux en auto-satin : contour exact (ellipse extérieure moins ellipse intérieure), cousus
    // après les traits dans la couleur des traits (aucun changement de fil).
    const std::size_t beforeRings = out.embroideries.size();
    std::vector<geometry::PathSet> ringSets;
    for (const auto& r : rings) {
        const double ca = std::cos(r.angle), sa = std::sin(r.angle);
        const auto ellipse = [&](double a, double b, bool reversed) {
            constexpr int kSegments = 96;
            geometry::Path path;
            path.closed = true;
            for (int i = 0; i < kSegments; ++i) {
                const double t =
                    2.0 * std::numbers::pi * (reversed ? kSegments - i : i) / kSegments;
                const double ex = a * std::cos(t), ey = b * std::sin(t);
                const double ix = r.cx + ex * ca - ey * sa;
                const double iy = r.cy + ex * sa + ey * ca;
                geometry::PathNode node;
                node.pos = Vec2um{Micrometers{static_cast<std::int32_t>(
                                      std::lround((ix - seg.width / 2.0) * px_mm * 1000.0))},
                                  Micrometers{static_cast<std::int32_t>(
                                      std::lround((seg.height / 2.0 - iy) * px_mm * 1000.0))}};
                path.nodes.push_back(node);
            }
            return path;
        };
        document::VectorObject vec;
        vec.id = ids.next();
        vec.name = "Anneau " + std::to_string(out.embroideries.size() - beforeRings + 1);
        vec.rgb = lineRgb;
        geometry::PathSet set;
        set.outer = ellipse(r.a + r.width, r.b + r.width, false);
        set.holes.push_back(ellipse(r.a, r.b, true));
        vec.paths.push_back(std::move(set));
        document::EmbroideryObject emb;
        emb.id = ids.next();
        emb.name = "Satin " + vec.name;
        emb.source_vector = vec.id;
        emb.rgb = lineRgb;
        emb.intent = document::EmbroideryIntent::AutoChoice;
        emb.params = document::AutoSatinParams{};
        ringSets.push_back(vec.paths.front());
        out.vectors.push_back(std::move(vec));
        out.embroideries.push_back(std::move(emb));
    }
    // L'anneau remplace la matière de la couleur des traits sous lui : on le retire des
    // remplissages de cette couleur (sinon marine + satin + lignes = surépaisseur).
    if (!ringSets.empty()) {
        std::vector<ObjectId> fillSources;
        for (const auto& e : out.embroideries) {
            if (e.rgb == lineRgb) {
                fillSources.push_back(e.source_vector);
            }
        }
        for (auto& v : out.vectors) {
            if (v.rgb != lineRgb || v.name.rfind("Anneau", 0) == 0 ||
                std::find(fillSources.begin(), fillSources.end(), v.id) == fillSources.end()) {
                continue;
            }
            auto cut = geometry::difference_polygons(v.paths, ringSets);
            if (cut && !cut->empty()) {
                v.paths = std::move(*cut);
            }
        }
    }
    if (metrics != nullptr) {
        metrics->ring_objects = out.embroideries.size() - beforeRings;
        metrics->line_pixels = stitchPixels;
        metrics->line_lightness = plan.line_lightness;
        metrics->line_rgb = lineRgb;
        metrics->separation_pixels = plan.separation_pixels;
        metrics->bar_pixels = barsPixels;
        metrics->short_fragment_pixels = plan.short_fragment_pixels;
        metrics->bar_objects = barObjectsEnd - fillObjects;
        metrics->reassigned_pixels = plan.reassigned_pixels;
        metrics->contours = cm;
        metrics->line_objects = beforeRings - barObjectsEnd;
    }
    return out;
}

} // namespace openstitch::autodigitize
