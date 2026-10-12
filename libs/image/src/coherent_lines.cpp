// SPDX-License-Identifier: Apache-2.0
//
// Lignes cohérentes — implémentation d'après la publication :
//   H. Kang, S. Lee, C. K. Chui, « Coherent Line Drawing », NPAR 2007
//   (champ de flux de tangentes + DoG orientée, FDoG).
// Écrite à partir de l'article (aucun code tiers repris). Calcul séquentiel en
// virgule flottante simple précision : sortie déterministe octet pour octet.
#include "coherent_lines.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace openstitch::image::detail {

namespace {

struct Vec {
    float x{0.0F};
    float y{0.0F};
};

struct Field {
    int w{0};
    int h{0};
    std::vector<float> v;

    Field(int width, int height, float init = 0.0F)
        : w(width), h(height),
          v(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), init) {}

    [[nodiscard]] std::size_t idx(int x, int y) const {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
               static_cast<std::size_t>(x);
    }
    [[nodiscard]] float at(int x, int y) const {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return v[idx(x, y)];
    }
    // Échantillon bilinéaire, bords répliqués.
    [[nodiscard]] float sample(float fx, float fy) const {
        const float cx = std::clamp(fx, 0.0F, static_cast<float>(w - 1));
        const float cy = std::clamp(fy, 0.0F, static_cast<float>(h - 1));
        const int x0 = static_cast<int>(cx);
        const int y0 = static_cast<int>(cy);
        const int x1 = std::min(x0 + 1, w - 1);
        const int y1 = std::min(y0 + 1, h - 1);
        const float ax = cx - static_cast<float>(x0);
        const float ay = cy - static_cast<float>(y0);
        const float top = v[idx(x0, y0)] * (1.0F - ax) + v[idx(x1, y0)] * ax;
        const float bot = v[idx(x0, y1)] * (1.0F - ax) + v[idx(x1, y1)] * ax;
        return top * (1.0F - ay) + bot * ay;
    }
};

float gauss(float s, float sigma) {
    if (sigma <= 0.0F) {
        return 0.0F;
    }
    return std::exp(-(s * s) / (2.0F * sigma * sigma)) / (sigma * 2.5066283F);
}

// Luminance 0..1 ; les pixels (quasi) transparents prennent le blanc.
Field luminance(const Image& in) {
    Field L(in.width, in.height);
    for (int y = 0; y < in.height; ++y) {
        for (int x = 0; x < in.width; ++x) {
            const std::uint8_t* p = in.rgba.data() + L.idx(x, y) * 4;
            if (p[3] < 128) {
                L.v[L.idx(x, y)] = 1.0F;
                continue;
            }
            L.v[L.idx(x, y)] =
                (0.299F * static_cast<float>(p[0]) + 0.587F * static_cast<float>(p[1]) +
                 0.114F * static_cast<float>(p[2])) /
                255.0F;
        }
    }
    return L;
}

// Gradient de Sobel normalisé par 8 ; magnitude normalisée dans [0,1].
void gradient(const Field& L, Field& gx, Field& gy, Field& mag) {
    float maxMag = 0.0F;
    for (int y = 0; y < L.h; ++y) {
        for (int x = 0; x < L.w; ++x) {
            const float a = L.at(x - 1, y - 1);
            const float b = L.at(x, y - 1);
            const float c = L.at(x + 1, y - 1);
            const float d = L.at(x - 1, y);
            const float f = L.at(x + 1, y);
            const float g = L.at(x - 1, y + 1);
            const float h = L.at(x, y + 1);
            const float i = L.at(x + 1, y + 1);
            const float sx = ((c + 2.0F * f + i) - (a + 2.0F * d + g)) / 8.0F;
            const float sy = ((g + 2.0F * h + i) - (a + 2.0F * b + c)) / 8.0F;
            gx.v[L.idx(x, y)] = sx;
            gy.v[L.idx(x, y)] = sy;
            const float m = std::sqrt(sx * sx + sy * sy);
            mag.v[L.idx(x, y)] = m;
            maxMag = std::max(maxMag, m);
        }
    }
    if (maxMag > 0.0F) {
        for (float& m : mag.v) {
            m /= maxMag;
        }
    }
}

// Champ de tangentes de flux (§3.1 de l'article) : tangente initiale = gradient
// tourné de 90°, puis lissage itératif pondéré (magnitude, direction) qui aligne
// les tangentes voisines sans écraser les contours forts.
std::vector<Vec> build_etf(const Field& gx, const Field& gy, const Field& mag, int iterations,
                           int radius) {
    const int w = mag.w;
    const int h = mag.h;
    std::vector<Vec> t(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = mag.idx(x, y);
            const float m = std::sqrt(gx.v[i] * gx.v[i] + gy.v[i] * gy.v[i]);
            if (m > 1e-6F) {
                t[i] = {-gy.v[i] / m, gx.v[i] / m};
            } else {
                t[i] = {0.0F, 0.0F};
            }
        }
    }
    constexpr float kEta = 1.0F;
    std::vector<Vec> next = t;
    for (int it = 0; it < iterations; ++it) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t i = mag.idx(x, y);
                const Vec tx = t[i];
                float sumX = 0.0F;
                float sumY = 0.0F;
                const int y0 = std::max(0, y - radius);
                const int y1 = std::min(h - 1, y + radius);
                const int x0 = std::max(0, x - radius);
                const int x1 = std::min(w - 1, x + radius);
                for (int yy = y0; yy <= y1; ++yy) {
                    for (int xx = x0; xx <= x1; ++xx) {
                        const float dx = static_cast<float>(xx - x);
                        const float dy = static_cast<float>(yy - y);
                        if (dx * dx + dy * dy > static_cast<float>(radius * radius)) {
                            continue; // w_s : disque
                        }
                        const std::size_t j = mag.idx(xx, yy);
                        const Vec ty = t[j];
                        const float dot = tx.x * ty.x + tx.y * ty.y;
                        const float phi = dot >= 0.0F ? 1.0F : -1.0F;
                        const float wm = (1.0F + std::tanh(kEta * (mag.v[j] - mag.v[i]))) * 0.5F;
                        const float wd = std::fabs(dot);
                        const float wgt = phi * wm * wd;
                        sumX += ty.x * wgt;
                        sumY += ty.y * wgt;
                    }
                }
                const float n = std::sqrt(sumX * sumX + sumY * sumY);
                // Zone plate (aucun gradient) : on garde la tangente voisine moyenne
                // si elle existe, sinon le champ reste nul.
                next[i] = n > 1e-6F ? Vec{sumX / n, sumY / n} : Vec{0.0F, 0.0F};
            }
        }
        t.swap(next);
    }
    return t;
}

} // namespace

Result<Image> apply_coherent_lines(const Image& in, const CoherentLinesOp& op) {
    if (!(op.sigma_c >= 0.3 && op.sigma_c <= 5.0)) {
        return fail(ErrorCategory::UserInput, "Échelle sigma_c invalide (0,3 à 5)");
    }
    if (!(op.sigma_m >= 0.0 && op.sigma_m <= 10.0)) {
        return fail(ErrorCategory::UserInput, "Lissage sigma_m invalide (0 à 10)");
    }
    if (!(op.rho >= 0.9 && op.rho <= 1.0)) {
        return fail(ErrorCategory::UserInput, "Paramètre rho invalide (0,9 à 1)");
    }
    if (!(op.tau >= 0.5 && op.tau <= 1.0)) {
        return fail(ErrorCategory::UserInput, "Seuil tau invalide (0,5 à 1)");
    }
    if (op.etf_iterations < 0 || op.etf_iterations > 10 || op.etf_radius < 1 ||
        op.etf_radius > 15) {
        return fail(ErrorCategory::UserInput, "Paramètres du champ de flux invalides");
    }

    const int w = in.width;
    const int h = in.height;
    const Field L = luminance(in);
    Field gx(w, h);
    Field gy(w, h);
    Field mag(w, h);
    gradient(L, gx, gy, mag);
    const std::vector<Vec> etf = build_etf(gx, gy, mag, op.etf_iterations, op.etf_radius);

    // --- DoG 1D le long de la normale au flux (= direction du gradient) ------
    const auto sigmaC = static_cast<float>(op.sigma_c);
    const float sigmaS = 1.6F * sigmaC;
    const auto rho = static_cast<float>(op.rho);
    const int halfWidth = std::max(1, static_cast<int>(std::ceil(3.0F * sigmaS)));
    std::vector<float> dogKernel(static_cast<std::size_t>(halfWidth) + 1);
    for (int s = 0; s <= halfWidth; ++s) {
        const auto fs = static_cast<float>(s);
        dogKernel[static_cast<std::size_t>(s)] = gauss(fs, sigmaC) - rho * gauss(fs, sigmaS);
    }

    Field H(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Vec t = etf[H.idx(x, y)];
            if (t.x == 0.0F && t.y == 0.0F) {
                H.v[H.idx(x, y)] = 0.0F; // aucune structure : réponse DoG nulle
                continue;
            }
            const Vec n{t.y, -t.x}; // normale au flux
            float acc = dogKernel[0] * L.v[H.idx(x, y)];
            for (int s = 1; s <= halfWidth; ++s) {
                const auto fs = static_cast<float>(s);
                const float k = dogKernel[static_cast<std::size_t>(s)];
                acc += k *
                       L.sample(static_cast<float>(x) + n.x * fs, static_cast<float>(y) + n.y * fs);
                acc += k *
                       L.sample(static_cast<float>(x) - n.x * fs, static_cast<float>(y) - n.y * fs);
            }
            H.v[H.idx(x, y)] = acc;
        }
    }

    // --- Lissage de H le long du flux (courbe de flux, pas de 1 px) ----------
    Field F = H;
    const auto sigmaM = static_cast<float>(op.sigma_m);
    if (sigmaM > 0.0F) {
        const int steps = std::max(1, static_cast<int>(std::ceil(3.0F * sigmaM)));
        std::vector<float> wk(static_cast<std::size_t>(steps) + 1);
        float norm = gauss(0.0F, sigmaM);
        for (int s = 1; s <= steps; ++s) {
            wk[static_cast<std::size_t>(s)] = gauss(static_cast<float>(s), sigmaM);
        }
        wk[0] = norm;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const Vec t0 = etf[H.idx(x, y)];
                if (t0.x == 0.0F && t0.y == 0.0F) {
                    continue;
                }
                float acc = wk[0] * H.v[H.idx(x, y)];
                float sum = wk[0];
                for (int dir = -1; dir <= 1; dir += 2) {
                    float px = static_cast<float>(x);
                    float py = static_cast<float>(y);
                    Vec prev{t0.x * static_cast<float>(dir), t0.y * static_cast<float>(dir)};
                    for (int s = 1; s <= steps; ++s) {
                        px += prev.x;
                        py += prev.y;
                        if (px < 0.0F || py < 0.0F || px > static_cast<float>(w - 1) ||
                            py > static_cast<float>(h - 1)) {
                            break;
                        }
                        const Vec tn = etf[H.idx(static_cast<int>(std::lround(px)),
                                                 static_cast<int>(std::lround(py)))];
                        if (tn.x == 0.0F && tn.y == 0.0F) {
                            break;
                        }
                        // Cohérence d'orientation : on garde le sens de marche.
                        const float sgn = (tn.x * prev.x + tn.y * prev.y) >= 0.0F ? 1.0F : -1.0F;
                        prev = {tn.x * sgn, tn.y * sgn};
                        const float k = wk[static_cast<std::size_t>(s)];
                        acc += k * H.sample(px, py);
                        sum += k;
                    }
                }
                F.v[H.idx(x, y)] = acc / sum;
            }
        }
    }

    // --- Seuillage (éq. 9 de l'article) : trait noir si F < 0 et 1 + tanh(F) < tau.
    Image out;
    out.width = w;
    out.height = h;
    out.source_had_alpha = in.source_had_alpha;
    out.rgba.assign(in.rgba.size(), 255);
    const auto tau = static_cast<float>(op.tau);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = F.idx(x, y);
            std::uint8_t* p = out.rgba.data() + i * 4;
            p[3] = in.rgba[i * 4 + 3];
            // Les coefficients de la DoG sont normalisés en luminance 0..1 ; on
            // amplifie pour que tanh sature sur de vrais contours (échelle de
            // l'article : intensités 0..255 -> facteur 255 / 10).
            const float f = F.v[i] * 25.5F;
            const bool line = p[3] >= 128 && f < 0.0F && (1.0F + std::tanh(f)) < tau;
            const std::uint8_t v = line ? 0 : 255;
            p[0] = v;
            p[1] = v;
            p[2] = v;
        }
    }
    return out;
}

} // namespace openstitch::image::detail
