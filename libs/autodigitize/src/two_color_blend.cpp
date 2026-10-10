// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/two_color_blend.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

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

} // namespace openstitch::autodigitize
