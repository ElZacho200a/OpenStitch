// SPDX-License-Identifier: Apache-2.0
#include "openstitch/segmentation/fine_structures.hpp"

#include "openstitch/core/parallel.hpp"
#include "openstitch/core/pipeline_profile.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace openstitch::segmentation {

namespace {

constexpr std::uint8_t kAlphaOpaque = 128;
// L* d'OpenCV (8 bits) vaut L* x 2,55.
constexpr float kLScale = 100.0f / 255.0f;

// Écart CIELAB (ΔE76) entre deux pixels lus dans les trois plans Lab 8 bits.
float delta_e(const cv::Mat& lab, int x1, int y1, int x2, int y2) {
    const auto* p = lab.ptr<std::uint8_t>(y1) + x1 * 3;
    const auto* q = lab.ptr<std::uint8_t>(y2) + x2 * 3;
    const float dl = (static_cast<float>(p[0]) - static_cast<float>(q[0])) * kLScale;
    const float da = static_cast<float>(p[1]) - static_cast<float>(q[1]);
    const float db = static_cast<float>(p[2]) - static_cast<float>(q[2]);
    return std::sqrt(dl * dl + da * da + db * db);
}

std::vector<int> kernel_diameters(int max_width_px) {
    // Un noyau de diamètre d ne remplit qu'un trait de largeur <= d - 2 de façon fiable (cercle
    // discret, anticrénelage) : le plus grand noyau vérifie d >= max_width_px + 2.
    std::vector<int> scales{3};
    int d = 3;
    while (d < max_width_px + 2) {
        d = 2 * d - 1;
        scales.push_back(d);
    }
    return scales;
}

} // namespace

Result<FineStructureMap> detect_fine_structures(const image::Image& img,
                                                const FineStructureOptions& options) {
    if (img.empty() || img.width <= 0 || img.height <= 0 ||
        img.rgba.size() <
            static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 4) {
        return fail(ErrorCategory::UserInput, "Aucune image pour la détection des traits fins");
    }
    if (options.max_width_px < 1 || options.max_width_px > 32 || options.low_contrast <= 0.0 ||
        options.high_contrast < options.low_contrast || options.min_length_px < 1 ||
        options.ridge_side_ratio <= 0.0 || options.ridge_side_ratio > 1.0) {
        return fail(ErrorCategory::UserInput, "Options de détection des traits fins invalides");
    }

    const int w = img.width;
    const int h = img.height;
    const auto pixelCount = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);

    cv::Mat rgb(h, w, CV_8UC3);
    std::vector<std::uint8_t> opaque(pixelCount, 0);
    for (int y = 0; y < h; ++y) {
        auto* dst = rgb.ptr<std::uint8_t>(y);
        for (int x = 0; x < w; ++x) {
            const std::uint8_t* px =
                &img.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                           static_cast<std::size_t>(x)) *
                          4];
            dst[x * 3 + 0] = px[0];
            dst[x * 3 + 1] = px[1];
            dst[x * 3 + 2] = px[2];
            opaque[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                   static_cast<std::size_t>(x)] = px[3] >= kAlphaOpaque ? 1 : 0;
        }
    }
    cv::Mat lab;
    cv::cvtColor(rgb, lab, cv::COLOR_RGB2Lab);
    std::vector<cv::Mat> planes;
    cv::split(lab, planes);

    const auto isOpaque = [&](int x, int y) {
        return opaque[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                      static_cast<std::size_t>(x)] != 0;
    };

    const float low = static_cast<float>(options.low_contrast);
    const float high = static_cast<float>(options.high_contrast);
    const float halfContrast = 0.5f * low;
    const auto ratio = static_cast<float>(options.ridge_side_ratio);

    // Candidats (>= seuil bas, crête validée) et germes (>= seuil haut), au plus petit noyau qui
    // répond : un trait fin n'est pas « repris » par un noyau plus large.
    std::vector<std::uint8_t> candidate(pixelCount, 0);
    std::vector<std::uint8_t> seed(pixelCount, 0);
    std::vector<std::uint8_t> widthOf(pixelCount, 0);
    std::vector<float> response(pixelCount, 0.0f);

    for (const int d : kernel_diameters(options.max_width_px)) {
        const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(d, d));
        std::array<cv::Mat, 3> hat;
        for (std::size_t c = 0; c < 3; ++c) {
            cv::Mat black;
            cv::Mat white;
            {
                const auto bytes = planes[c].total();
                const pipeline_profile::Scope profile(pipeline_profile::Stage::Morphology,
                                                      bytes + kernel.total(), bytes, 2);
                cv::morphologyEx(planes[c], black, cv::MORPH_BLACKHAT, kernel);
                cv::morphologyEx(planes[c], white, cv::MORPH_TOPHAT, kernel);
            }
            hat[c] = cv::max(black, white);
        }
        const int reachFar = d - 1;
        const int reachNear = std::max(2, reachFar / 2 + 1);
        const std::array<int, 2> reaches{reachFar, reachNear};
        constexpr std::array<std::array<int, 2>, 4> kDirs{{{1, 0}, {0, 1}, {1, 1}, {1, -1}}};

        // Une tâche par ligne : lit des données constantes (hat, lab, opaque), n'écrit que dans les
        // cases de sa ligne -- même résultat quel que soit le nombre de fils.
        parallel_for(static_cast<std::size_t>(h), [&](std::size_t row) {
            const int y = static_cast<int>(row);
            const auto* hl = hat[0].ptr<std::uint8_t>(y);
            const auto* ha = hat[1].ptr<std::uint8_t>(y);
            const auto* hb = hat[2].ptr<std::uint8_t>(y);
            for (int x = 0; x < w; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                      static_cast<std::size_t>(x);
                if (opaque[i] == 0 || candidate[i] != 0) {
                    continue;
                }
                const float dl = static_cast<float>(hl[x]) * kLScale;
                const float da = static_cast<float>(ha[x]);
                const float db = static_cast<float>(hb[x]);
                const float resp = std::sqrt(dl * dl + da * da + db * db);
                if (resp < low) {
                    continue;
                }
                bool ridge = false;
                for (const int reach : reaches) {
                    for (const auto& dir : kDirs) {
                        const int x1 = x + dir[0] * reach;
                        const int y1 = y + dir[1] * reach;
                        const int x2 = x - dir[0] * reach;
                        const int y2 = y - dir[1] * reach;
                        if (x1 < 0 || x1 >= w || x2 < 0 || x2 >= w || y1 < 0 || y1 >= h || y2 < 0 ||
                            y2 >= h || !isOpaque(x1, y1) || !isOpaque(x2, y2)) {
                            continue;
                        }
                        const float d1 = delta_e(lab, x, y, x1, y1);
                        const float d2 = delta_e(lab, x, y, x2, y2);
                        if (d1 < halfContrast || d2 < halfContrast) {
                            continue;
                        }
                        // Le centre est hors de l'axe des deux côtés (pas une rampe) : un trait.
                        if (delta_e(lab, x1, y1, x2, y2) <= ratio * (d1 + d2)) {
                            ridge = true;
                            break;
                        }
                    }
                    if (ridge) {
                        break;
                    }
                }
                if (!ridge) {
                    continue;
                }
                candidate[i] = 1;
                response[i] = resp;
                widthOf[i] = static_cast<std::uint8_t>(reachFar);
                seed[i] = resp >= high ? 1 : 0;
            }
        });
    }

    // Hystérésis : composantes 8-connexes des candidats, retenues si elles contiennent un germe et
    // ne sont pas trop courtes. (Les aplats gardent leur 4-connexité : seul le masque de traits est
    // relié en diagonale.)
    FineStructureMap map;
    map.width = w;
    map.height = h;
    map.mask.assign(pixelCount, 0);
    map.confidence.assign(pixelCount, 0);
    map.width_px.assign(pixelCount, 0);

    cv::Mat candMat(h, w, CV_8U);
    for (int y = 0; y < h; ++y) {
        auto* out = candMat.ptr<std::uint8_t>(y);
        for (int x = 0; x < w; ++x) {
            out[x] = candidate[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                               static_cast<std::size_t>(x)] != 0
                         ? 255
                         : 0;
        }
    }
    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int n = cv::connectedComponentsWithStats(candMat, labels, stats, centroids, 8, CV_32S);
    std::vector<std::uint8_t> hasSeed(static_cast<std::size_t>(n), 0);
    for (int y = 0; y < h; ++y) {
        const int* lab32 = labels.ptr<int>(y);
        for (int x = 0; x < w; ++x) {
            if (lab32[x] > 0 && seed[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                     static_cast<std::size_t>(x)] != 0) {
                hasSeed[static_cast<std::size_t>(lab32[x])] = 1;
            }
        }
    }
    // Largeur moyenne (borne du noyau) de chaque composante : un trait est plus long que large. Le
    // bout arrondi d'un gros aplat (coin que la fermeture arrondit) répond comme un trait épais et
    // court : il est écarté par cette exigence de longueur >= largeur.
    std::vector<std::uint64_t> widthSum(static_cast<std::size_t>(n), 0);
    for (int y = 0; y < h; ++y) {
        const int* lab32 = labels.ptr<int>(y);
        for (int x = 0; x < w; ++x) {
            if (lab32[x] > 0) {
                widthSum[static_cast<std::size_t>(lab32[x])] +=
                    widthOf[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                            static_cast<std::size_t>(x)];
            }
        }
    }
    std::vector<std::uint8_t> keep(static_cast<std::size_t>(n), 0);
    for (int c = 1; c < n; ++c) {
        if (hasSeed[static_cast<std::size_t>(c)] == 0) {
            continue;
        }
        const int extent =
            std::max(stats.at<int>(c, cv::CC_STAT_WIDTH), stats.at<int>(c, cv::CC_STAT_HEIGHT));
        const auto area = static_cast<std::uint64_t>(stats.at<int>(c, cv::CC_STAT_AREA));
        const int meanWidth = static_cast<int>(widthSum[static_cast<std::size_t>(c)] / area);
        if (extent < std::max(options.min_length_px, meanWidth)) {
            ++map.rejected_short;
            continue;
        }
        keep[static_cast<std::size_t>(c)] = 1;
        ++map.component_count;
    }
    for (int y = 0; y < h; ++y) {
        const int* lab32 = labels.ptr<int>(y);
        for (int x = 0; x < w; ++x) {
            if (lab32[x] <= 0 || keep[static_cast<std::size_t>(lab32[x])] == 0) {
                continue;
            }
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                  static_cast<std::size_t>(x);
            map.mask[i] = 1;
            map.width_px[i] = widthOf[i];
            map.confidence[i] = static_cast<std::uint8_t>(
                std::clamp(std::lround(255.0f * response[i] / (2.0f * high)), 1L, 255L));
            ++map.pixel_count;
            if (seed[i] != 0) {
                ++map.seed_count;
            }
        }
    }
    return map;
}

} // namespace openstitch::segmentation
