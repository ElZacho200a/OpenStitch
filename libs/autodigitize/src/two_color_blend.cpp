// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/two_color_blend.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "openstitch/geometry/polyline.hpp"
#include "openstitch/image/ops.hpp"

namespace openstitch::autodigitize {

namespace {

using V3 = std::array<double, 3>;

V3 to_cmy(std::array<std::uint8_t, 3> rgb) {
    return {1.0 - rgb[0] / 255.0, 1.0 - rgb[1] / 255.0, 1.0 - rgb[2] / 255.0};
}

std::array<std::uint8_t, 3> from_cmy(const V3& cmy) {
    std::array<std::uint8_t, 3> rgb{};
    for (std::size_t k = 0; k < 3; ++k) {
        rgb[k] = static_cast<std::uint8_t>(std::lround(std::clamp(1.0 - cmy[k], 0.0, 1.0) * 255.0));
    }
    return rgb;
}

double dot3(const V3& a, const V3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Écart entre lignes pour une proportion t de s2 : b / t, borné.
std::int32_t spacing_for(double t, const TwoColorBlendOptions& o) {
    const double b = static_cast<double>(o.thread_width.value);
    const double raw = b / std::max(t, 1e-6);
    return static_cast<std::int32_t>(std::lround(std::clamp(
        raw, static_cast<double>(o.min_spacing.value), static_cast<double>(o.max_spacing.value))));
}

Vec2um to_um(double x, double y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(y))}};
}

} // namespace

std::optional<TwoColorBlend> analyze_two_color_blend(const std::vector<ColorSample>& samples,
                                                     const TwoColorBlendOptions& options) {
    const std::size_t n = samples.size();
    if (n < 2) {
        return std::nullopt;
    }

    // --- ACP des couleurs en CMY -------------------------------------------
    std::vector<V3> cmy;
    cmy.reserve(n);
    V3 mean{0.0, 0.0, 0.0};
    for (const auto& s : samples) {
        cmy.push_back(to_cmy(s.rgb));
        for (std::size_t k = 0; k < 3; ++k) {
            mean[k] += cmy.back()[k];
        }
    }
    for (double& m : mean) {
        m /= static_cast<double>(n);
    }
    double cov[3][3] = {};
    for (const V3& c : cmy) {
        const V3 d{c[0] - mean[0], c[1] - mean[1], c[2] - mean[2]};
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) {
                cov[i][j] += d[i] * d[j];
            }
        }
    }
    for (auto& row : cov) {
        for (double& v : row) {
            v /= static_cast<double>(n);
        }
    }
    // Itération de la puissance, départ = colonne de plus grande norme (pas de
    // hasard : même entrée, même axe).
    V3 axis{};
    double bestNorm = -1.0;
    for (std::size_t j = 0; j < 3; ++j) {
        const V3 col{cov[0][j], cov[1][j], cov[2][j]};
        const double nn = std::sqrt(dot3(col, col));
        if (nn > bestNorm) {
            bestNorm = nn;
            axis = col;
        }
    }
    if (bestNorm < 1e-12) {
        return std::nullopt; // toutes les couleurs identiques
    }
    for (int it = 0; it < 64; ++it) {
        V3 next{};
        for (std::size_t i = 0; i < 3; ++i) {
            next[i] = cov[i][0] * axis[0] + cov[i][1] * axis[1] + cov[i][2] * axis[2];
        }
        const double nn = std::sqrt(dot3(next, next));
        if (nn < 1e-15) {
            break;
        }
        for (std::size_t i = 0; i < 3; ++i) {
            axis[i] = next[i] / nn;
        }
    }
    {
        const double nn = std::sqrt(dot3(axis, axis));
        for (double& a : axis) {
            a /= nn;
        }
        // Signe canonique : la composante dominante est positive.
        std::size_t big = 0;
        for (std::size_t k = 1; k < 3; ++k) {
            if (std::abs(axis[k]) > std::abs(axis[big])) {
                big = k;
            }
        }
        if (axis[big] < 0.0) {
            for (double& a : axis) {
                a = -a;
            }
        }
    }

    std::vector<double> proj(n);
    for (std::size_t i = 0; i < n; ++i) {
        proj[i] = dot3({cmy[i][0] - mean[0], cmy[i][1] - mean[1], cmy[i][2] - mean[2]}, axis);
    }
    std::vector<double> sorted = proj;
    std::sort(sorted.begin(), sorted.end());
    const double frac = std::clamp(options.outlier_fraction, 0.0, 0.49);
    const double lo =
        sorted[static_cast<std::size_t>(std::floor(frac * static_cast<double>(n - 1)))];
    const double hi =
        sorted[static_cast<std::size_t>(std::ceil((1.0 - frac) * static_cast<double>(n - 1)))];
    if (hi - lo < 1e-3) {
        return std::nullopt; // moins d'un niveau de couleur d'écart : rien à fondre
    }

    std::vector<double> t(n);
    double meanT = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        t[i] = std::clamp((proj[i] - lo) / (hi - lo), 0.0, 1.0);
        meanT += t[i];
    }
    meanT /= static_cast<double>(n);

    const V3 endLo{mean[0] + axis[0] * lo, mean[1] + axis[1] * lo, mean[2] + axis[2] * lo};
    const V3 endHi{mean[0] + axis[0] * hi, mean[1] + axis[1] * hi, mean[2] + axis[2] * hi};
    TwoColorBlend out;
    if (meanT > 0.5) {
        // La couleur du côté « haut » est la mieux représentée : elle fait le fond.
        out.background = from_cmy(endHi);
        out.foreground = from_cmy(endLo);
        for (double& v : t) {
            v = 1.0 - v;
        }
        meanT = 1.0 - meanT;
    } else {
        out.background = from_cmy(endLo);
        out.foreground = from_cmy(endHi);
    }
    out.mean_t = meanT;

    // --- Plan t(x, y) par moindres carrés (mm, centré) ---------------------
    double cx = 0.0;
    double cy = 0.0;
    for (const auto& s : samples) {
        cx += static_cast<double>(s.pos.x.value);
        cy += static_cast<double>(s.pos.y.value);
    }
    cx /= static_cast<double>(n);
    cy /= static_cast<double>(n);

    double sxx = 0.0;
    double sxy = 0.0;
    double syy = 0.0;
    double sxt = 0.0;
    double syt = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = (static_cast<double>(samples[i].pos.x.value) - cx) / 1000.0;
        const double y = (static_cast<double>(samples[i].pos.y.value) - cy) / 1000.0;
        const double dt = t[i] - meanT;
        sxx += x * x;
        sxy += x * y;
        syy += y * y;
        sxt += x * dt;
        syt += y * dt;
    }
    const double det = sxx * syy - sxy * sxy;
    double gx = 0.0;
    double gy = 0.0;
    if (std::abs(det) > 1e-9 * std::max(1.0, sxx * syy)) {
        gx = (sxt * syy - syt * sxy) / det;
        gy = (syt * sxx - sxt * sxy) / det;
    }
    const double gradient = std::sqrt(gx * gx + gy * gy); // variation de t par mm

    bool uniform = gradient < 1e-12;
    double smin = 0.0;
    double smax = 0.0;
    double dx = 0.0;
    double dy = 0.0;
    if (!uniform) {
        dx = gx / gradient;
        dy = gy / gradient;
        bool first = true;
        for (const auto& s : samples) {
            const double x = (static_cast<double>(s.pos.x.value) - cx) / 1000.0;
            const double y = (static_cast<double>(s.pos.y.value) - cy) / 1000.0;
            const double d = x * dx + y * dy;
            smin = first ? d : std::min(smin, d);
            smax = first ? d : std::max(smax, d);
            first = false;
        }
        uniform = gradient * (smax - smin) < options.min_ramp;
    }

    if (uniform) {
        const auto spacing = Micrometers{spacing_for(meanT, options)};
        const Vec2um c = to_um(cx, cy);
        out.foreground_density = document::DensityGradient{c, c, spacing, spacing};
    } else {
        const double tFrom = std::clamp(meanT + gradient * smin, 0.0, 1.0);
        const double tTo = std::clamp(meanT + gradient * smax, 0.0, 1.0);
        out.foreground_density = document::DensityGradient{
            to_um(cx + dx * smin * 1000.0, cy + dy * smin * 1000.0),
            to_um(cx + dx * smax * 1000.0, cy + dy * smax * 1000.0),
            Micrometers{spacing_for(tFrom, options)}, Micrometers{spacing_for(tTo, options)}};
    }
    return out;
}

namespace {

// Polygone aplati (contour extérieur ou trou) pour le test pair-impair.
struct FlatRing {
    std::vector<std::pair<double, double>> pts;
};

bool inside_even_odd(const std::vector<FlatRing>& rings, double x, double y) {
    bool inside = false;
    for (const auto& ring : rings) {
        const auto& p = ring.pts;
        for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
            const bool straddles = (p[i].second > y) != (p[j].second > y);
            if (straddles &&
                x < (p[j].first - p[i].first) * (y - p[i].second) / (p[j].second - p[i].second) +
                        p[i].first) {
                inside = !inside;
            }
        }
    }
    return inside;
}

} // namespace

std::vector<ColorSample> sample_region_colors(const image::Image& image, Millimeters mm_per_px,
                                              const std::vector<geometry::PathSet>& shape,
                                              std::size_t max_samples) {
    std::vector<ColorSample> out;
    if (image.empty() || mm_per_px.value <= 0.0 || max_samples == 0) {
        return out;
    }
    std::vector<FlatRing> rings;
    for (const auto& set : shape) {
        const auto flat = geometry::flatten_to_polygon(set, Micrometers{50});
        const auto add = [&rings](const geometry::Path& path) {
            if (path.nodes.size() < 3) {
                return;
            }
            FlatRing ring;
            for (const auto& node : path.nodes) {
                ring.pts.emplace_back(static_cast<double>(node.pos.x.value),
                                      static_cast<double>(node.pos.y.value));
            }
            rings.push_back(std::move(ring));
        };
        add(flat.outer);
        for (const auto& hole : flat.holes) {
            add(hole);
        }
    }
    if (rings.empty()) {
        return out;
    }

    const double pxUm = mm_per_px.value * 1000.0;
    const double w = image.width;
    const double h = image.height;
    // Boîte englobante de la forme en pixels.
    double minX = 1e300;
    double maxX = -1e300;
    double minY = 1e300;
    double maxY = -1e300;
    for (const auto& ring : rings) {
        for (const auto& [x, y] : ring.pts) {
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(minX / pxUm + w / 2.0 - 0.5)));
    const int x1 = std::min(image.width - 1, static_cast<int>(std::ceil(maxX / pxUm + w / 2.0)));
    const int y0 = std::max(0, static_cast<int>(std::floor(h / 2.0 - maxY / pxUm - 0.5)));
    const int y1 = std::min(image.height - 1, static_cast<int>(std::ceil(h / 2.0 - minY / pxUm)));
    if (x1 < x0 || y1 < y0) {
        return out;
    }
    const double boxPixels = static_cast<double>(x1 - x0 + 1) * static_cast<double>(y1 - y0 + 1);
    const int stride = std::max(
        1, static_cast<int>(std::ceil(std::sqrt(boxPixels / static_cast<double>(max_samples)))));
    for (int py = y0; py <= y1; py += stride) {
        for (int px = x0; px <= x1; px += stride) {
            const double xUm = (px + 0.5 - w / 2.0) * pxUm;
            const double yUm = (h / 2.0 - (py + 0.5)) * pxUm;
            if (!inside_even_odd(rings, xUm, yUm)) {
                continue;
            }
            const std::uint8_t* pixel =
                image.rgba.data() +
                (static_cast<std::size_t>(py) * static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(px)) *
                    4;
            if (pixel[3] < 128) {
                continue;
            }
            out.push_back({to_um(xUm, yUm), {pixel[0], pixel[1], pixel[2]}});
        }
    }
    return out;
}

Result<RegionBlend> analyze_region_blend(const image::Image& image, Millimeters mm_per_px,
                                         const std::vector<geometry::PathSet>& shape,
                                         const thread_palette::ThreadChart* chart,
                                         const TwoColorBlendOptions& options) {
    if (image.empty()) {
        return fail(ErrorCategory::UserInput, "Aucune image à analyser");
    }
    // Lissage bilatéral : le grain et le bruit JPEG ne doivent pas fausser l'ACP.
    const auto smooth = image::apply_op(image, image::BilateralDenoiseOp{2});
    const image::Image& source = smooth ? *smooth : image;
    const auto samples = sample_region_colors(source, mm_per_px, shape);
    if (samples.size() < 2) {
        return fail(ErrorCategory::UserInput, "La forme ne recouvre aucun pixel de l'image");
    }
    const auto blend = analyze_two_color_blend(samples, options);
    if (!blend) {
        return fail(ErrorCategory::UserInput,
                    "Les couleurs de cette forme sont trop uniformes pour un fondu à deux fils");
    }
    RegionBlend result;
    result.blend = *blend;
    if (chart != nullptr) {
        result.threads =
            thread_palette::best_thread_pair(blend->background, blend->foreground, *chart);
    }
    return result;
}

} // namespace openstitch::autodigitize
