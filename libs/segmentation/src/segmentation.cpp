// SPDX-License-Identifier: Apache-2.0
#include "openstitch/segmentation/segmentation.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>
#include <string>
#include <set>

namespace openstitch::segmentation {

namespace {

constexpr std::uint8_t kAlphaOpaque = 128; // seuil : en dessous, pixel de fond

std::size_t slot_of(RegionId id) {
    return static_cast<std::size_t>(id.value - 1);
}

RegionId id_of_slot(std::size_t slot) {
    return RegionId{slot + 1};
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

} // namespace

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
    return static_cast<std::size_t>(std::count_if(region_slots.begin(), region_slots.end(),
                                                  [](const auto& s) { return s.has_value(); }));
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
        auto* dst =
            rgb.ptr<std::uint8_t>(static_cast<int>(i / static_cast<std::size_t>(img.width)),
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
            cv::boxFilter(classMask, density[static_cast<std::size_t>(c)], CV_32F,
                          cv::Size(ksize, ksize), cv::Point(-1, -1), false);
        }
        cv::Mat smoothedColorIdx = colorIdx.clone();
        for (int y = 0; y < img.height; ++y) {
            for (int x = 0; x < img.width; ++x) {
                if (colorIdx.at<int>(y, x) < 0) {
                    continue; // pixel transparent : jamais reclasse
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
            if (seg.region_slots[s] && seg.region_slots[s]->pixel_count <
                                           static_cast<std::size_t>(options.min_region_px)) {
                order.push_back(s);
            }
        }
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return seg.region_slots[a]->pixel_count < seg.region_slots[b]->pixel_count;
        });
        // Performance (audit 2026-09, docs/performance-audit.md) : l'ancienne
        // boucle balayait l'image ENTIÈRE deux fois par petite région
        // (recherche de la voisine majoritaire puis réétiquetage), soit O(petites régions x
        // pixels) -- 38 s sur la fixture tentabrode sans lissage. Chaque
        // petite région garde désormais la liste de ses pixels ; une région
        // absorbée transmet la sienne à sa cible si celle-ci est une petite
        // région pas encore traitée (la seule qui en ait besoin). Mêmes
        // comptes de frontière, même départage (std::map : plus petit label),
        // même ordre de traitement : résultat identique (test
        // test_segmentation.cpp « nettoyage des petites regions ... reference »).
        std::vector<std::vector<std::uint32_t>> pixelsOf(seg.region_slots.size());
        std::vector<char> pending(seg.region_slots.size(), 0);
        for (const std::size_t s : order) {
            pending[s] = 1;
        }
        for (std::uint32_t i = 0; i < seg.labels.size(); ++i) {
            const std::uint32_t label = seg.labels[i];
            if (label != 0 && pending[label - 1]) {
                pixelsOf[label - 1].push_back(i);
            }
        }
        const int w = seg.width;
        const int h = seg.height;
        for (const std::size_t s : order) {
            if (!seg.region_slots[s]) {
                continue;
            }
            const std::uint32_t label = static_cast<std::uint32_t>(s + 1);
            std::vector<std::uint32_t> pixels = std::move(pixelsOf[s]);
            std::map<std::uint32_t, std::size_t> counts;
            for (const std::uint32_t i : pixels) {
                const int x = static_cast<int>(i % static_cast<std::uint32_t>(w));
                const int y = static_cast<int>(i / static_cast<std::uint32_t>(w));
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
            const std::uint32_t target = counts.empty()
                                             ? 0
                                             : std::max_element(counts.begin(), counts.end(),
                                                                [](const auto& a, const auto& b) {
                                                                    return a.second < b.second;
                                                                })
                                                   ->first;
            for (const std::uint32_t i : pixels) {
                seg.labels[i] = target;
            }
            if (target != 0) {
                seg.region_slots[target - 1]->pixel_count += pixels.size();
                if (pending[target - 1]) {
                    auto& dst = pixelsOf[target - 1];
                    dst.insert(dst.end(), pixels.begin(), pixels.end());
                }
            }
            pending[s] = 0;
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

std::vector<RegionBorder> region_adjacency(const Segmentation& seg) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> counts;
    const auto at = [&](int x, int y) {
        return seg.labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(seg.width) +
                          static_cast<std::size_t>(x)];
    };
    const auto add = [&](std::uint32_t p, std::uint32_t q) {
        if (p == q || p == 0 || q == 0) {
            return;
        }
        ++counts[{std::min(p, q), std::max(p, q)}];
    };
    for (int y = 0; y < seg.height; ++y) {
        for (int x = 0; x < seg.width; ++x) {
            if (x + 1 < seg.width) {
                add(at(x, y), at(x + 1, y));
            }
            if (y + 1 < seg.height) {
                add(at(x, y), at(x, y + 1));
            }
        }
    }
    std::vector<RegionBorder> out;
    out.reserve(counts.size());
    for (const auto& [key, length] : counts) {
        out.push_back({RegionId{key.first}, RegionId{key.second}, length});
    }
    return out;
}

std::size_t remove_thin_parts(Segmentation& seg, int min_width_px) {
    if (min_width_px < 2 || seg.width <= 0 || seg.height <= 0) {
        return 0;
    }
    const int w = seg.width;
    const int h = seg.height;
    const auto idx = [w](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
               static_cast<std::size_t>(x);
    };

    // Boîte englobante de chaque label (ROI de l'ouverture).
    const std::size_t slots = seg.region_slots.size();
    std::vector<cv::Rect> boxes(slots + 1);
    std::vector<char> seen(slots + 1, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::uint32_t l = seg.labels[idx(x, y)];
            if (l == 0) {
                continue;
            }
            if (!seen[l]) {
                boxes[l] = cv::Rect(x, y, 1, 1);
                seen[l] = 1;
            } else {
                boxes[l] |= cv::Rect(x, y, 1, 1);
            }
        }
    }

    const cv::Mat kernel =
        cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(min_width_px, min_width_px));
    const std::vector<std::uint32_t> orig = seg.labels;
    std::vector<char> pending(orig.size(), 0);
    for (std::uint32_t l = 1; l <= slots; ++l) {
        if (!seen[l] || !seg.region_slots[l - 1]) {
            continue;
        }
        // ROI élargie du diamètre du noyau ; hors image, OpenCV traite le
        // bord comme intérieur (valeur par défaut) : le bord de l'image
        // n'érode pas une région qui le touche.
        const cv::Rect roi = (boxes[l] + cv::Size(2 * min_width_px, 2 * min_width_px) -
                              cv::Point(min_width_px, min_width_px)) &
                             cv::Rect(0, 0, w, h);
        cv::Mat mask(roi.height, roi.width, CV_8U, cv::Scalar(0));
        for (int y = 0; y < roi.height; ++y) {
            for (int x = 0; x < roi.width; ++x) {
                if (orig[idx(roi.x + x, roi.y + y)] == l) {
                    mask.at<std::uint8_t>(y, x) = 255;
                }
            }
        }
        cv::Mat opened;
        cv::morphologyEx(mask, opened, cv::MORPH_OPEN, kernel);
        for (int y = 0; y < roi.height; ++y) {
            for (int x = 0; x < roi.width; ++x) {
                if (mask.at<std::uint8_t>(y, x) != 0 && opened.at<std::uint8_t>(y, x) == 0) {
                    pending[idx(roi.x + x, roi.y + y)] = 1;
                }
            }
        }
    }

    // Réaffectation de proche en proche depuis les régions VOISINES (jamais
    // depuis la région d'origine du pixel) ; passes synchrones pour rester
    // indépendant de l'ordre de balayage.
    std::size_t moved = 0;
    for (;;) {
        std::vector<std::pair<std::size_t, std::uint32_t>> assign;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t i = idx(x, y);
                if (!pending[i]) {
                    continue;
                }
                std::map<std::uint32_t, int> votes;
                const auto vote = [&](int nx, int ny) {
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                        return;
                    }
                    const std::size_t j = idx(nx, ny);
                    if (!pending[j] && seg.labels[j] != 0 && seg.labels[j] != orig[i]) {
                        ++votes[seg.labels[j]];
                    }
                };
                vote(x - 1, y);
                vote(x + 1, y);
                vote(x, y - 1);
                vote(x, y + 1);
                if (!votes.empty()) {
                    assign.emplace_back(i, std::max_element(votes.begin(), votes.end(),
                                                            [](const auto& a, const auto& b) {
                                                                return a.second < b.second;
                                                            })
                                               ->first);
                }
            }
        }
        if (assign.empty()) {
            break;
        }
        for (const auto& [i, l] : assign) {
            seg.labels[i] = l;
            pending[i] = 0;
        }
        moved += assign.size();
    }

    // Comptes de pixels ; une région vidée disparaît.
    for (auto& slot : seg.region_slots) {
        if (slot) {
            slot->pixel_count = 0;
        }
    }
    for (const std::uint32_t l : seg.labels) {
        if (l != 0) {
            ++seg.region_slots[l - 1]->pixel_count;
        }
    }
    for (auto& slot : seg.region_slots) {
        if (slot && slot->pixel_count == 0) {
            slot.reset();
        }
    }
    return moved;
}

std::size_t merge_small_regions(Segmentation& seg, std::size_t min_px,
                                std::optional<std::array<std::uint8_t, 3>> excluded,
                                double boundary_weight) {
    const std::size_t slots = seg.region_slots.size();
    std::vector<std::size_t> count(slots + 1, 0);
    for (std::size_t s = 0; s < slots; ++s) {
        if (seg.region_slots[s]) {
            count[s + 1] = seg.region_slots[s]->pixel_count;
        }
    }
    // Voisinage pondéré par la longueur de frontière, mis à jour à chaque fusion.
    std::vector<std::map<std::uint32_t, std::size_t>> nb(slots + 1);
    for (const auto& b : region_adjacency(seg)) {
        const auto a = static_cast<std::uint32_t>(b.a.value);
        const auto c = static_cast<std::uint32_t>(b.b.value);
        nb[a][c] += b.length;
        nb[c][a] += b.length;
    }
    const auto isExcluded = [&](std::uint32_t l) {
        return excluded && seg.region_slots[l - 1]->rgb == *excluded;
    };
    std::vector<std::uint32_t> parent(slots + 1);
    for (std::uint32_t l = 0; l <= slots; ++l) {
        parent[l] = l;
    }
    // Taille effective (cf. `boundary_weight`) ; clé de la file.
    const auto effective = [&](std::uint32_t l) {
        std::size_t border = 0;
        for (const auto& [other, length] : nb[l]) {
            border += length;
        }
        // Pick : un polygone par les centres des pixels de bord d'une tache de
        // N pixels et B arêtes de bord a une aire N - B/2 + 1 (rectangle a x b :
        // (a-1)(b-1)) -- généralisé au poids w : N - w.B + 2w.
        return static_cast<double>(count[l]) - boundary_weight * static_cast<double>(border) +
               2.0 * boundary_weight;
    };
    const double minSize = static_cast<double>(min_px);
    std::set<std::pair<double, std::uint32_t>> queue;
    std::vector<double> key(slots + 1, 0.0);
    for (std::uint32_t l = 1; l <= slots; ++l) {
        if (seg.region_slots[l - 1]) {
            key[l] = effective(l);
            if (key[l] < minSize) {
                queue.insert({key[l], l});
            }
        }
    }
    std::size_t merges = 0;
    while (!queue.empty()) {
        const std::uint32_t small = queue.begin()->second;
        queue.erase(queue.begin());
        std::uint32_t keep = 0;
        std::size_t best = 0;
        for (const auto& [other, length] : nb[small]) {
            if (!isExcluded(other) && length > best) {
                best = length;
                keep = other;
            }
        }
        if (keep == 0) {
            continue; // aucune voisine admissible : fragment isolé conservé
        }
        queue.erase({key[keep], keep});
        count[keep] += count[small];
        for (const auto& [other, length] : nb[small]) {
            if (other == keep) {
                continue;
            }
            nb[keep][other] += length;
            nb[other][keep] += length;
            nb[other].erase(small);
        }
        nb[keep].erase(small);
        nb[small].clear();
        parent[small] = keep;
        ++merges;
        // Les voisines de `small` ont maintenant `keep` pour voisine : leur
        // frontière totale est inchangée (longueurs transférées), seule la
        // clé de `keep` bouge.
        key[keep] = effective(keep);
        if (key[keep] < minSize) {
            queue.insert({key[keep], keep});
        }
    }
    if (merges == 0) {
        return 0;
    }
    const auto root = [&](std::uint32_t l) {
        while (parent[l] != l) {
            l = parent[l];
        }
        return l;
    };
    for (auto& l : seg.labels) {
        if (l != 0) {
            l = root(l);
        }
    }
    for (std::uint32_t l = 1; l <= slots; ++l) {
        if (!seg.region_slots[l - 1]) {
            continue;
        }
        if (parent[l] != l) {
            seg.region_slots[l - 1].reset();
        } else {
            seg.region_slots[l - 1]->pixel_count = count[l];
        }
    }
    return merges;
}

double cielab_lightness(std::array<std::uint8_t, 3> rgb) {
    // sRGB -> luminance relative Y (linéarisation IEC 61966-2-1), puis L*.
    const auto linear = [](std::uint8_t v) {
        const double c = static_cast<double>(v) / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const double y = 0.2126 * linear(rgb[0]) + 0.7152 * linear(rgb[1]) + 0.0722 * linear(rgb[2]);
    constexpr double kEpsilon = 216.0 / 24389.0;
    constexpr double kKappa = 24389.0 / 27.0;
    return y > kEpsilon ? 116.0 * std::cbrt(y) - 16.0 : kKappa * y;
}

std::optional<BackgroundCandidate> background_candidate(const Segmentation& seg,
                                                        const BackgroundCandidateOptions& options) {
    std::optional<std::size_t> largest;
    for (std::size_t s = 0; s < seg.region_slots.size(); ++s) {
        if (seg.region_slots[s] && (!largest || seg.region_slots[s]->pixel_count >
                                                    seg.region_slots[*largest]->pixel_count)) {
            largest = s;
        }
    }
    if (!largest || seg.width <= 0 || seg.height <= 0) {
        return std::nullopt;
    }
    BackgroundCandidate out;
    out.region = seg.region_slots[*largest]->id;
    out.rgb = seg.region_slots[*largest]->rgb;
    out.lightness = cielab_lightness(out.rgb);

    // Labels (slot+1) de TOUTES les régions de cette couleur exacte.
    std::vector<char> sameColor(seg.region_slots.size() + 1, 0);
    for (std::size_t s = 0; s < seg.region_slots.size(); ++s) {
        if (seg.region_slots[s] && seg.region_slots[s]->rgb == out.rgb) {
            sameColor[s + 1] = 1;
        }
    }
    const auto isBg = [&](int x, int y) {
        const std::uint32_t label =
            seg.labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(seg.width) +
                       static_cast<std::size_t>(x)];
        return label != 0 && sameColor[label] != 0;
    };
    std::size_t count = 0;
    for (int y = 0; y < seg.height; ++y) {
        for (int x = 0; x < seg.width; ++x) {
            count += isBg(x, y) ? 1 : 0;
        }
    }
    out.area_ratio = static_cast<double>(count) /
                     (static_cast<double>(seg.width) * static_cast<double>(seg.height));

    bool top = false, bottom = false, left = false, right = false;
    for (int x = 0; x < seg.width; ++x) {
        top = top || isBg(x, 0);
        bottom = bottom || isBg(x, seg.height - 1);
    }
    for (int y = 0; y < seg.height; ++y) {
        left = left || isBg(0, y);
        right = right || isBg(seg.width - 1, y);
    }
    out.sides_touched = int{top} + int{bottom} + int{left} + int{right};
    out.recommended =
        out.lightness > options.min_lightness && out.sides_touched >= options.min_sides_touched;
    return out;
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
        px[0] = selected
                    ? static_cast<std::uint8_t>(region.rgb[0] + (255 - region.rgb[0]) * 55 / 100)
                    : region.rgb[0];
        px[1] = selected
                    ? static_cast<std::uint8_t>(region.rgb[1] + (255 - region.rgb[1]) * 55 / 100)
                    : region.rgb[1];
        px[2] = selected
                    ? static_cast<std::uint8_t>(region.rgb[2] + (255 - region.rgb[2]) * 55 / 100)
                    : region.rgb[2];
        px[3] = 255;
    }
    return out;
}

std::vector<RegionId> all_regions(const Segmentation& seg) {
    std::vector<RegionId> out;
    for (const auto& slot : seg.region_slots) {
        if (slot) {
            out.push_back(slot->id);
        }
    }
    return out; // les slots sont par identifiant croissant
}

std::vector<RegionId> regions_with_color(const Segmentation& seg, std::array<std::uint8_t, 3> rgb) {
    std::vector<RegionId> out;
    for (const auto& slot : seg.region_slots) {
        if (slot && slot->rgb == rgb) {
            out.push_back(slot->id);
        }
    }
    return out;
}

std::vector<RegionId> neighbors_of(const Segmentation& seg, RegionId id) {
    std::vector<std::pair<std::size_t, RegionId>> found;
    for (const auto& border : region_adjacency(seg)) {
        if (border.a == id) {
            found.push_back({border.length, border.b});
        } else if (border.b == id) {
            found.push_back({border.length, border.a});
        }
    }
    std::sort(found.begin(), found.end(), [](const auto& l, const auto& r) {
        return l.first != r.first ? l.first > r.first : l.second.value < r.second.value;
    });
    std::vector<RegionId> out;
    out.reserve(found.size());
    for (const auto& f : found) {
        out.push_back(f.second);
    }
    return out;
}

std::vector<RegionId> regions_in_rect(const Segmentation& seg, int x0, int y0, int x1, int y1) {
    if (x0 > x1) {
        std::swap(x0, x1);
    }
    if (y0 > y1) {
        std::swap(y0, y1);
    }
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, seg.width - 1);
    y1 = std::min(y1, seg.height - 1);
    std::set<std::uint32_t> labels;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const std::uint32_t l = seg.labels[static_cast<std::size_t>(y) *
                                                   static_cast<std::size_t>(seg.width) +
                                               static_cast<std::size_t>(x)];
            if (l != 0) {
                labels.insert(l);
            }
        }
    }
    std::vector<RegionId> out;
    for (const std::uint32_t l : labels) {
        out.push_back(RegionId{l});
    }
    return out;
}

Result<std::array<std::uint8_t, 3>> region_mean_color(const Segmentation& seg,
                                                      const image::Image& original, RegionId id) {
    if (seg.find(id) == nullptr) {
        return fail(ErrorCategory::Internal, "Région introuvable",
                    "mean id=" + std::to_string(id.value));
    }
    if (original.width != seg.width || original.height != seg.height) {
        return fail(ErrorCategory::UserInput,
                    "L'image d'origine n'a pas la taille de la segmentation");
    }
    std::uint64_t sum[3] = {0, 0, 0};
    std::uint64_t count = 0;
    const auto label = static_cast<std::uint32_t>(id.value);
    for (std::size_t i = 0; i < seg.labels.size(); ++i) {
        if (seg.labels[i] != label) {
            continue;
        }
        const std::uint8_t* px = original.rgba.data() + i * 4;
        sum[0] += px[0];
        sum[1] += px[1];
        sum[2] += px[2];
        ++count;
    }
    if (count == 0) {
        return fail(ErrorCategory::Internal, "Région vide");
    }
    const auto avg = [count](std::uint64_t s) {
        return static_cast<std::uint8_t>((s + count / 2) / count);
    };
    return std::array<std::uint8_t, 3>{avg(sum[0]), avg(sum[1]), avg(sum[2])};
}

image::Image render_map_multi(const Segmentation& seg, const std::vector<RegionId>& highlights) {
    image::Image out = render_map(seg, std::nullopt);
    for (std::size_t i = 0; i < seg.labels.size(); ++i) {
        const std::uint32_t label = seg.labels[i];
        if (label == 0) {
            continue;
        }
        if (std::find(highlights.begin(), highlights.end(), RegionId{label}) == highlights.end()) {
            continue;
        }
        std::uint8_t* px = out.rgba.data() + i * 4;
        for (int c = 0; c < 3; ++c) { // même éclaircissement que `render_map` (55 % de blanc)
            px[c] = static_cast<std::uint8_t>(px[c] + (255 - px[c]) * 55 / 100);
        }
    }
    return out;
}

} // namespace openstitch::segmentation
