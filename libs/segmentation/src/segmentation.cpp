// SPDX-License-Identifier: Apache-2.0
#include "openstitch/segmentation/segmentation.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <limits>
#include <map>

namespace openstitch::segmentation {

namespace {

constexpr std::uint8_t kAlphaOpaque = 128;  // seuil : en dessous, pixel de fond

std::size_t slot_of(RegionId id) {
    return static_cast<std::size_t>(id.value - 1);
}

RegionId id_of_slot(std::size_t slot) {
    return RegionId{slot + 1};
}

// Voisine majoritaire (4-connexité) d'une région ; nullopt si isolée.
// Déterministe : en cas d'égalité, le plus petit label gagne (std::map trié).
std::optional<std::uint32_t> majority_neighbor(const Segmentation& seg, std::uint32_t label) {
    std::map<std::uint32_t, std::size_t> counts;
    const int w = seg.width;
    const int h = seg.height;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (seg.labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                           static_cast<std::size_t>(x)] != label) {
                continue;
            }
            const auto visit = [&](int nx, int ny) {
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                    return;
                }
                const std::uint32_t other =
                    seg.labels[static_cast<std::size_t>(ny) * static_cast<std::size_t>(w) +
                               static_cast<std::size_t>(nx)];
                if (other != label && other != 0) {
                    ++counts[other];
                }
            };
            visit(x - 1, y);
            visit(x + 1, y);
            visit(x, y - 1);
            visit(x, y + 1);
        }
    }
    if (counts.empty()) {
        return std::nullopt;
    }
    return std::max_element(counts.begin(), counts.end(),
                            [](const auto& a, const auto& b) { return a.second < b.second; })
        ->first;
}

std::vector<std::uint32_t> relabel(Segmentation& seg, std::uint32_t from, std::uint32_t to) {
    std::vector<std::uint32_t> changed;
    for (std::uint32_t i = 0; i < seg.labels.size(); ++i) {
        if (seg.labels[i] == from) {
            seg.labels[i] = to;
            changed.push_back(i);
        }
    }
    return changed;
}

}  // namespace

const Region* Segmentation::find(RegionId id) const {
    if (!id.valid() || slot_of(id) >= region_slots.size() || !region_slots[slot_of(id)]) {
        return nullptr;
    }
    return &*region_slots[slot_of(id)];
}

Region* Segmentation::find(RegionId id) {
    return const_cast<Region*>(std::as_const(*this).find(id));
}

std::size_t Segmentation::region_count() const {
    return static_cast<std::size_t>(
        std::count_if(region_slots.begin(), region_slots.end(), [](const auto& s) { return s.has_value(); }));
}

Result<Segmentation> segment(const image::Image& img, const SegmentationOptions& options) {
    if (img.empty()) {
        return fail(ErrorCategory::Internal, "Aucune image à segmenter");
    }
    if (options.max_colors < 2 || options.max_colors > 64) {
        return fail(ErrorCategory::UserInput, "Nombre de couleurs invalide (2 à 64)");
    }

    const auto pixelCount =
        static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height);

    // RGB -> Lab (perceptuel) sur les pixels opaques uniquement.
    cv::Mat rgb(img.height, img.width, CV_8UC3);
    std::vector<std::uint8_t> opaque(pixelCount, 0);
    std::size_t opaqueCount = 0;
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const std::uint8_t* px = img.rgba.data() + i * 4;
        auto* dst = rgb.ptr<std::uint8_t>(static_cast<int>(i / static_cast<std::size_t>(img.width)),
                                          static_cast<int>(i % static_cast<std::size_t>(img.width)));
        dst[0] = px[0];
        dst[1] = px[1];
        dst[2] = px[2];
        if (px[3] >= kAlphaOpaque) {
            opaque[i] = 1;
            ++opaqueCount;
        }
    }
    if (opaqueCount == 0) {
        return fail(ErrorCategory::UserInput, "L'image est entièrement transparente");
    }

    cv::Mat lab;
    cv::cvtColor(rgb, lab, cv::COLOR_RGB2Lab);

    // k-means déterministe sur un échantillon de pixels opaques.
    constexpr std::size_t kMaxSamples = 20'000;
    const std::size_t stride = std::max<std::size_t>(1, opaqueCount / kMaxSamples);
    std::vector<cv::Vec3f> samplesVec;
    std::size_t seen = 0;
    for (std::size_t i = 0; i < pixelCount; ++i) {
        if (!opaque[i]) {
            continue;
        }
        if (seen++ % stride == 0) {
            const auto* px =
                lab.ptr<std::uint8_t>(static_cast<int>(i / static_cast<std::size_t>(img.width)),
                                      static_cast<int>(i % static_cast<std::size_t>(img.width)));
            samplesVec.emplace_back(px[0], px[1], px[2]);
        }
    }
    const int k = std::min<int>(options.max_colors, static_cast<int>(samplesVec.size()));
    cv::Mat samples(static_cast<int>(samplesVec.size()), 3, CV_32F, samplesVec.data());
    cv::Mat kmLabels;
    cv::Mat centers;
    cv::setRNGSeed(12345);
    cv::kmeans(samples, k, kmLabels,
               cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 20, 1.0), 3,
               cv::KMEANS_PP_CENTERS, centers);

    // Couleur RGB représentative de chaque centre Lab.
    cv::Mat centersLab8(1, k, CV_8UC3);
    for (int c = 0; c < k; ++c) {
        auto* px = centersLab8.ptr<std::uint8_t>(0, c);
        px[0] = static_cast<std::uint8_t>(std::lround(centers.at<float>(c, 0)));
        px[1] = static_cast<std::uint8_t>(std::lround(centers.at<float>(c, 1)));
        px[2] = static_cast<std::uint8_t>(std::lround(centers.at<float>(c, 2)));
    }
    cv::Mat centersRgb;
    cv::cvtColor(centersLab8, centersRgb, cv::COLOR_Lab2RGB);

    // Affectation de chaque pixel opaque au centre Lab le plus proche.
    cv::Mat colorIdx(img.height, img.width, CV_32S, cv::Scalar(-1));
    for (std::size_t i = 0; i < pixelCount; ++i) {
        if (!opaque[i]) {
            continue;
        }
        const int row = static_cast<int>(i / static_cast<std::size_t>(img.width));
        const int col = static_cast<int>(i % static_cast<std::size_t>(img.width));
        const auto* px = lab.ptr<std::uint8_t>(row, col);
        int best = 0;
        float bestDist = std::numeric_limits<float>::max();
        for (int c = 0; c < k; ++c) {
            const float dl = centers.at<float>(c, 0) - static_cast<float>(px[0]);
            const float da = centers.at<float>(c, 1) - static_cast<float>(px[1]);
            const float db = centers.at<float>(c, 2) - static_cast<float>(px[2]);
            const float d = dl * dl + da * da + db * db;
            if (d < bestDist) {
                bestDist = d;
                best = c;
            }
        }
        colorIdx.at<int>(row, col) = best;
    }

    // Lissage optionnel : vote local majoritaire par classe de couleur, pour
    // absorber le bruit poivre-et-sel de l'affectation pixel-à-pixel
    // ci-dessus avant l'extraction des composantes connexes (formes plus
    // lisses, sans laisser de trou : contrairement à une ouverture
    // morphologique appliquée séparément à chaque masque de couleur, un
    // pixel réaffecté rejoint toujours une classe existante).
    if (options.smoothing_radius_px > 0) {
        const int ksize = 2 * options.smoothing_radius_px + 1;
        std::vector<cv::Mat> density(static_cast<std::size_t>(k));
        for (int c = 0; c < k; ++c) {
            cv::Mat classMask(img.height, img.width, CV_32F, cv::Scalar(0.0f));
            for (int y = 0; y < img.height; ++y) {
                for (int x = 0; x < img.width; ++x) {
                    if (colorIdx.at<int>(y, x) == c) {
                        classMask.at<float>(y, x) = 1.0f;
                    }
                }
            }
            cv::boxFilter(classMask, density[static_cast<std::size_t>(c)], CV_32F, cv::Size(ksize, ksize),
                          cv::Point(-1, -1), false);
        }
        cv::Mat smoothedColorIdx = colorIdx.clone();
        for (int y = 0; y < img.height; ++y) {
            for (int x = 0; x < img.width; ++x) {
                if (colorIdx.at<int>(y, x) < 0) {
                    continue;  // pixel transparent : jamais reclasse
                }
                int bestClass = colorIdx.at<int>(y, x);
                float bestDensity = 0.0f;
                for (int c = 0; c < k; ++c) {
                    const float d = density[static_cast<std::size_t>(c)].at<float>(y, x);
                    if (d > bestDensity) {
                        bestDensity = d;
                        bestClass = c;
                    }
                }
                smoothedColorIdx.at<int>(y, x) = bestClass;
            }
        }
        colorIdx = smoothedColorIdx;
    }

    // Composantes connexes (4-connexité) par couleur -> régions à ids stables.
    Segmentation seg;
    seg.width = img.width;
    seg.height = img.height;
    seg.labels.assign(pixelCount, 0);
    for (int c = 0; c < k; ++c) {
        cv::Mat mask(img.height, img.width, CV_8U);
        for (int y = 0; y < img.height; ++y) {
            for (int x = 0; x < img.width; ++x) {
                mask.at<std::uint8_t>(y, x) = (colorIdx.at<int>(y, x) == c) ? 255 : 0;
            }
        }
        cv::Mat cc;
        const int n = cv::connectedComponents(mask, cc, 4, CV_32S);
        if (n <= 1) {
            continue;
        }
        const std::size_t base = seg.region_slots.size();
        const auto* rgbPx = centersRgb.ptr<std::uint8_t>(0, c);
        for (int comp = 1; comp < n; ++comp) {
            Region region;
            region.id = id_of_slot(base + static_cast<std::size_t>(comp) - 1);
            region.rgb = {rgbPx[0], rgbPx[1], rgbPx[2]};
            seg.region_slots.push_back(region);
        }
        for (int y = 0; y < img.height; ++y) {
            for (int x = 0; x < img.width; ++x) {
                const int comp = cc.at<int>(y, x);
                if (comp > 0) {
                    seg.labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                               static_cast<std::size_t>(x)] =
                        static_cast<std::uint32_t>(base + static_cast<std::size_t>(comp));
                }
            }
        }
    }
    for (const std::uint32_t label : seg.labels) {
        if (label != 0) {
            ++seg.region_slots[label - 1]->pixel_count;
        }
    }

    // Nettoyage des petites régions : absorbées par leur voisine majoritaire
    // (ou par le fond si isolées). Une passe, des plus petites aux plus grandes.
    if (options.min_region_px > 1) {
        std::vector<std::size_t> order;
        for (std::size_t s = 0; s < seg.region_slots.size(); ++s) {
            if (seg.region_slots[s] &&
                seg.region_slots[s]->pixel_count < static_cast<std::size_t>(options.min_region_px)) {
                order.push_back(s);
            }
        }
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return seg.region_slots[a]->pixel_count < seg.region_slots[b]->pixel_count;
        });
        for (const std::size_t s : order) {
            if (!seg.region_slots[s]) {
                continue;
            }
            const std::uint32_t label = static_cast<std::uint32_t>(s + 1);
            const auto neighbor = majority_neighbor(seg, label);
            const std::uint32_t target = neighbor.value_or(0);
            const auto changed = relabel(seg, label, target);
            if (target != 0) {
                seg.region_slots[target - 1]->pixel_count += changed.size();
            }
            seg.region_slots[s].reset();
        }
    }

    return seg;
}

std::optional<RegionId> region_at(const Segmentation& seg, int x, int y) {
    if (x < 0 || y < 0 || x >= seg.width || y >= seg.height) {
        return std::nullopt;
    }
    const std::uint32_t label =
        seg.labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(seg.width) +
                   static_cast<std::size_t>(x)];
    if (label == 0) {
        return std::nullopt;
    }
    return id_of_slot(label - 1);
}

Result<std::vector<std::uint32_t>> merge_regions(Segmentation& seg, RegionId keep,
                                                 RegionId absorb) {
    if (keep == absorb) {
        return fail(ErrorCategory::UserInput, "Impossible de fusionner une région avec elle-même");
    }
    Region* keepRegion = seg.find(keep);
    Region* absorbRegion = seg.find(absorb);
    if (keepRegion == nullptr || absorbRegion == nullptr) {
        return fail(ErrorCategory::Internal, "Région introuvable",
                    "merge keep=" + std::to_string(keep.value) +
                        " absorb=" + std::to_string(absorb.value));
    }
    const auto changed = relabel(seg, static_cast<std::uint32_t>(absorb.value),
                                 static_cast<std::uint32_t>(keep.value));
    keepRegion->pixel_count += changed.size();
    seg.region_slots[slot_of(absorb)].reset();
    return changed;
}

Result<std::pair<RegionId, std::vector<std::uint32_t>>> remove_region(Segmentation& seg,
                                                                      RegionId id) {
    if (seg.find(id) == nullptr) {
        return fail(ErrorCategory::Internal, "Région introuvable",
                    "remove id=" + std::to_string(id.value));
    }
    // Supprimer = faire disparaître la région : ses pixels retournent au fond
    // (label 0), la région ne colore plus rien. On NE l'absorbe PAS dans une
    // voisine — cela ressemblerait à une fusion involontaire. Pour transférer
    // une région à une autre, l'utilisateur dispose de la fusion explicite.
    const std::uint32_t label = static_cast<std::uint32_t>(id.value);
    auto changed = relabel(seg, label, 0);
    seg.region_slots[slot_of(id)].reset();
    // absorbeur invalide : les pixels sont allés au fond, pas à une région.
    return std::pair{RegionId{}, std::move(changed)};
}

Result<std::array<std::uint8_t, 3>> recolor_region(Segmentation& seg, RegionId id,
                                                   std::array<std::uint8_t, 3> rgb) {
    Region* region = seg.find(id);
    if (region == nullptr) {
        return fail(ErrorCategory::Internal, "Région introuvable",
                    "recolor id=" + std::to_string(id.value));
    }
    const auto old = region->rgb;
    region->rgb = rgb;
    return old;
}

image::Image render_map(const Segmentation& seg, std::optional<RegionId> highlight) {
    image::Image out;
    out.width = seg.width;
    out.height = seg.height;
    out.source_had_alpha = true;
    out.rgba.assign(static_cast<std::size_t>(seg.width) * static_cast<std::size_t>(seg.height) * 4,
                    0);
    for (std::size_t i = 0; i < seg.labels.size(); ++i) {
        const std::uint32_t label = seg.labels[i];
        if (label == 0) {
            continue;
        }
        const Region& region = *seg.region_slots[label - 1];
        std::uint8_t* px = out.rgba.data() + i * 4;
        const bool selected = highlight && region.id == *highlight;
        // Sélection : couleur éclaircie (mélange 55 % blanc).
        px[0] = selected ? static_cast<std::uint8_t>(region.rgb[0] + (255 - region.rgb[0]) * 55 / 100)
                         : region.rgb[0];
        px[1] = selected ? static_cast<std::uint8_t>(region.rgb[1] + (255 - region.rgb[1]) * 55 / 100)
                         : region.rgb[1];
        px[2] = selected ? static_cast<std::uint8_t>(region.rgb[2] + (255 - region.rgb[2]) * 55 / 100)
                         : region.rgb[2];
        px[3] = 255;
    }
    return out;
}

}  // namespace openstitch::segmentation
