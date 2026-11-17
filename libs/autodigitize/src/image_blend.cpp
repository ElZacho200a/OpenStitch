// SPDX-License-Identifier: Apache-2.0
#include "openstitch/autodigitize/image_blend.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <utility>

#include "openstitch/autodigitize/two_color_blend.hpp"
#include "openstitch/core/parallel.hpp"

namespace openstitch::autodigitize {

namespace {

using V3 = std::array<double, 3>;

double sq(double v) {
    return v * v;
}

double dist2(const V3& a, const V3& b) {
    return sq(a[0] - b[0]) + sq(a[1] - b[1]) + sq(a[2] - b[2]);
}

double linear_to_srgb_unit(double c) {
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

Rgb8 to_rgb8(const V3& lin) {
    Rgb8 out{};
    for (int k = 0; k < 3; ++k) {
        out[static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(
            std::lround(linear_to_srgb_unit(lin[static_cast<std::size_t>(k)]) * 255.0));
    }
    return out;
}

V3 to_linear(const Rgb8& c) {
    return {srgb_to_linear(c[0]), srgb_to_linear(c[1]), srgb_to_linear(c[2])};
}

// Résout A x = b (n ≤ 5) par élimination de Gauss à pivot partiel. Faux si singulier.
bool solve_small(std::array<std::array<double, 6>, 5>& a, int n, std::array<double, 5>& x) {
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int r = col + 1; r < n; ++r) {
            if (std::abs(a[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)]) >
                std::abs(a[static_cast<std::size_t>(pivot)][static_cast<std::size_t>(col)])) {
                pivot = r;
            }
        }
        if (std::abs(a[static_cast<std::size_t>(pivot)][static_cast<std::size_t>(col)]) < 1e-14) {
            return false;
        }
        std::swap(a[static_cast<std::size_t>(col)], a[static_cast<std::size_t>(pivot)]);
        for (int r = col + 1; r < n; ++r) {
            const double f = a[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)] /
                             a[static_cast<std::size_t>(col)][static_cast<std::size_t>(col)];
            for (int c = col; c <= n; ++c) {
                a[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] -=
                    f * a[static_cast<std::size_t>(col)][static_cast<std::size_t>(c)];
            }
        }
    }
    for (int r = n - 1; r >= 0; --r) {
        double v = a[static_cast<std::size_t>(r)][static_cast<std::size_t>(n)];
        for (int c = r + 1; c < n; ++c) {
            v -= a[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] *
                 x[static_cast<std::size_t>(c)];
        }
        x[static_cast<std::size_t>(r)] =
            v / a[static_cast<std::size_t>(r)][static_cast<std::size_t>(r)];
    }
    return true;
}

// Sous-ensembles de {0..n-1} de taille 1..k, en ordre lexicographique (fixe : départage).
std::vector<std::vector<int>> supports(int n, int k) {
    std::vector<std::vector<int>> out;
    std::vector<int> cur;
    const auto rec = [&](auto&& self, int start) -> void {
        if (!cur.empty()) {
            out.push_back(cur);
        }
        if (static_cast<int>(cur.size()) == k) {
            return;
        }
        for (int i = start; i < n; ++i) {
            cur.push_back(i);
            self(self, i + 1);
            cur.pop_back();
        }
    };
    rec(rec, 0);
    return out;
}

std::vector<double> mix_with(const V3& v, const std::vector<V3>& colors, double lambda,
                             const std::vector<std::vector<int>>& subsets, double* residual) {
    const int n = static_cast<int>(colors.size());
    std::vector<double> d(colors.size());
    for (std::size_t i = 0; i < colors.size(); ++i) {
        d[i] = dist2(colors[i], v);
    }
    double best = std::numeric_limits<double>::infinity();
    std::vector<double> bestW(colors.size(), 0.0);
    for (const auto& s : subsets) {
        const int k = static_cast<int>(s.size());
        std::array<double, 5> w{};
        if (k == 1) {
            w[0] = 1.0;
        } else {
            // KKT : [2G 1 ; 1ᵀ 0] [w ; μ] = [2Cᵀv − λd ; 1], G = CᵀC (+ ε I).
            std::array<std::array<double, 6>, 5> a{};
            for (int r = 0; r < k; ++r) {
                const auto& cr = colors[static_cast<std::size_t>(s[static_cast<std::size_t>(r)])];
                for (int c = 0; c < k; ++c) {
                    const auto& cc =
                        colors[static_cast<std::size_t>(s[static_cast<std::size_t>(c)])];
                    a[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] =
                        2.0 * (cr[0] * cc[0] + cr[1] * cc[1] + cr[2] * cc[2]) +
                        (r == c ? 1e-9 : 0.0);
                }
                a[static_cast<std::size_t>(r)][static_cast<std::size_t>(k)] = 1.0;
                a[static_cast<std::size_t>(r)][static_cast<std::size_t>(k + 1)] =
                    2.0 * (cr[0] * v[0] + cr[1] * v[1] + cr[2] * v[2]) -
                    lambda * d[static_cast<std::size_t>(s[static_cast<std::size_t>(r)])];
            }
            for (int c = 0; c < k; ++c) {
                a[static_cast<std::size_t>(k)][static_cast<std::size_t>(c)] = 1.0;
            }
            a[static_cast<std::size_t>(k)][static_cast<std::size_t>(k)] = 0.0;
            a[static_cast<std::size_t>(k)][static_cast<std::size_t>(k + 1)] = 1.0;
            // Colonne second membre à l'indice k+1 : réordonner pour solve_small (n = k+1).
            std::array<std::array<double, 6>, 5> m{};
            for (int r = 0; r <= k; ++r) {
                for (int c = 0; c <= k + 1; ++c) {
                    m[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] =
                        a[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
                }
            }
            std::array<double, 5> x{};
            if (!solve_small(m, k + 1, x)) {
                continue;
            }
            bool feasible = true;
            for (int i = 0; i < k; ++i) {
                if (x[static_cast<std::size_t>(i)] < -1e-12) {
                    feasible = false;
                    break;
                }
                w[static_cast<std::size_t>(i)] = std::max(0.0, x[static_cast<std::size_t>(i)]);
            }
            if (!feasible) {
                continue;
            }
        }
        V3 mix{};
        double penalty = 0.0;
        for (int i = 0; i < k; ++i) {
            const auto idx = static_cast<std::size_t>(s[static_cast<std::size_t>(i)]);
            for (std::size_t c = 0; c < 3; ++c) {
                mix[c] += w[static_cast<std::size_t>(i)] * colors[idx][c];
            }
            penalty += w[static_cast<std::size_t>(i)] * d[idx];
        }
        const double objective = dist2(mix, v) + lambda * penalty;
        if (objective < best - 1e-15) {
            best = objective;
            std::fill(bestW.begin(), bestW.end(), 0.0);
            for (int i = 0; i < k; ++i) {
                bestW[static_cast<std::size_t>(s[static_cast<std::size_t>(i)])] =
                    w[static_cast<std::size_t>(i)];
            }
            if (residual != nullptr) {
                *residual = dist2(mix, v);
            }
        }
    }
    (void)n;
    return bestW;
}

// Parts entières sur 255, somme exacte : plus forts restes (départage par indice croissant).
void quantize_into(const std::vector<double>& w, std::uint8_t* out) {
    const std::size_t n = w.size();
    std::vector<int> q(n);
    std::vector<std::pair<double, std::size_t>> rest(n);
    int total = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double v = std::clamp(w[i], 0.0, 1.0) * 255.0;
        q[i] = static_cast<int>(std::floor(v));
        total += q[i];
        rest[i] = {v - q[i], i};
    }
    std::stable_sort(rest.begin(), rest.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t r = 0; total < 255 && r < n; ++r) {
        ++q[rest[r].second];
        ++total;
    }
    for (std::size_t r = n; total > 255 && r > 0; --r) {
        auto& v = q[rest[r - 1].second];
        if (v > 0) {
            --v;
            --total;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = static_cast<std::uint8_t>(q[i]);
    }
}

struct Box {
    double minX{std::numeric_limits<double>::infinity()};
    double minY{std::numeric_limits<double>::infinity()};
    double maxX{-std::numeric_limits<double>::infinity()};
    double maxY{-std::numeric_limits<double>::infinity()};
};

Box shape_box(const std::vector<geometry::PathSet>& shape) {
    Box b;
    for (const auto& set : shape) {
        for (const auto& node : set.outer.nodes) {
            b.minX = std::min(b.minX, static_cast<double>(node.pos.x.value));
            b.maxX = std::max(b.maxX, static_cast<double>(node.pos.x.value));
            b.minY = std::min(b.minY, static_cast<double>(node.pos.y.value));
            b.maxY = std::max(b.maxY, static_cast<double>(node.pos.y.value));
        }
    }
    return b;
}

std::vector<ColorSample> all_shape_pixels(const image::Image& image, Millimeters mm_per_px,
                                          const std::vector<geometry::PathSet>& shape) {
    // Un pas d'échantillonnage de 1 : autant d'échantillons que de pixels de la boîte.
    const std::size_t pixels =
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    return sample_region_colors(image, mm_per_px, shape, std::max<std::size_t>(1, pixels));
}

} // namespace

double srgb_to_linear(std::uint8_t channel) {
    const double c = channel / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

std::vector<double> mix_weights(const std::array<double, 3>& target,
                                const std::vector<std::array<double, 3>>& colors, double locality,
                                std::size_t max_threads, double* residual) {
    if (colors.empty()) {
        return {};
    }
    const int k = static_cast<int>(std::clamp<std::size_t>(max_threads, 1, 4));
    const auto subsets = supports(static_cast<int>(colors.size()), k);
    return mix_with(target, colors, std::max(0.0, locality), subsets, residual);
}

Result<document::WeightGrid> image_blend_weights(const image::Image& image, Millimeters mm_per_px,
                                                 const std::vector<geometry::PathSet>& shape,
                                                 const std::vector<Rgb8>& palette,
                                                 const ImageBlendOptions& options) {
    if (image.empty() || mm_per_px.value <= 0.0) {
        return fail(ErrorCategory::UserInput, "Aucune image à analyser");
    }
    if (palette.size() < 2 || palette.size() > 8) {
        return fail(ErrorCategory::UserInput, "Un fondu depuis l'image demande de 2 à 8 fils");
    }
    const Box box = shape_box(shape);
    if (!(box.maxX > box.minX) || !(box.maxY > box.minY)) {
        return fail(ErrorCategory::UserInput, "La forme est vide");
    }
    const double cell = std::clamp(static_cast<double>(options.cell.value), 100.0, 20'000.0);
    const auto columns = static_cast<std::int32_t>(std::floor((box.maxX - box.minX) / cell)) + 1;
    const auto rows = static_cast<std::int32_t>(std::floor((box.maxY - box.minY) / cell)) + 1;
    if (static_cast<std::int64_t>(columns) * rows > 1'000'000) {
        return fail(ErrorCategory::UserInput,
                    "Forme trop grande pour ce pas de grille : augmentez le pas");
    }
    const double ox = box.minX + cell / 2.0;
    const double oy = box.minY + cell / 2.0;

    // Couleur moyenne (RVB linéaire) des pixels de la forme dans chaque case.
    const auto samples = all_shape_pixels(image, mm_per_px, shape);
    if (samples.empty()) {
        return fail(ErrorCategory::UserInput, "La forme ne recouvre aucun pixel de l'image");
    }
    const auto cells = static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows);
    std::vector<V3> sum(cells, V3{});
    std::vector<int> count(cells, 0);
    for (const auto& s : samples) {
        const auto c = std::clamp(static_cast<std::int32_t>(std::lround(
                                      (static_cast<double>(s.pos.x.value) - ox) / cell)),
                                  0, columns - 1);
        const auto r = std::clamp(static_cast<std::int32_t>(std::lround(
                                      (static_cast<double>(s.pos.y.value) - oy) / cell)),
                                  0, rows - 1);
        const auto idx = static_cast<std::size_t>(r) * static_cast<std::size_t>(columns) +
                         static_cast<std::size_t>(c);
        const V3 lin = to_linear(s.rgb);
        for (std::size_t k = 0; k < 3; ++k) {
            sum[idx][k] += lin[k];
        }
        ++count[idx];
    }
    std::vector<V3> color(cells, V3{});
    std::vector<bool> filled(cells, false);
    for (std::size_t i = 0; i < cells; ++i) {
        if (count[i] > 0) {
            for (std::size_t k = 0; k < 3; ++k) {
                color[i][k] = sum[i][k] / count[i];
            }
            filled[i] = true;
        }
    }
    // Cases vides : propagation en largeur depuis les cases remplies (ordre fixe).
    {
        std::vector<std::size_t> frontier;
        for (std::size_t i = 0; i < cells; ++i) {
            if (filled[i]) {
                frontier.push_back(i);
            }
        }
        while (!frontier.empty()) {
            std::vector<std::size_t> next;
            for (const std::size_t i : frontier) {
                const auto r = static_cast<std::int32_t>(i / static_cast<std::size_t>(columns));
                const auto c = static_cast<std::int32_t>(i % static_cast<std::size_t>(columns));
                const std::array<std::pair<int, int>, 4> nb{{{0, -1}, {-1, 0}, {1, 0}, {0, 1}}};
                for (const auto& [dc, dr] : nb) {
                    const int nc = c + dc;
                    const int nr = r + dr;
                    if (nc < 0 || nr < 0 || nc >= columns || nr >= rows) {
                        continue;
                    }
                    const auto j =
                        static_cast<std::size_t>(nr) * static_cast<std::size_t>(columns) +
                        static_cast<std::size_t>(nc);
                    if (!filled[j]) {
                        filled[j] = true;
                        color[j] = color[i];
                        next.push_back(j);
                    }
                }
            }
            frontier = std::move(next);
        }
    }

    std::vector<V3> lin;
    lin.reserve(palette.size());
    for (const auto& p : palette) {
        lin.push_back(to_linear(p));
    }
    const int k = static_cast<int>(std::clamp<std::size_t>(options.max_threads_per_point, 2, 4));
    const auto subsets = supports(static_cast<int>(lin.size()), k);
    const double lambda = std::isfinite(options.locality) ? std::max(0.0, options.locality) : 0.0;
    const double minShare =
        std::isfinite(options.min_share) ? std::clamp(options.min_share, 0.0, 0.5) : 0.0;

    document::WeightGrid grid;
    grid.origin = Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(ox))},
                         Micrometers{static_cast<std::int32_t>(std::lround(oy))}};
    grid.cell = Micrometers{static_cast<std::int32_t>(cell)};
    grid.columns = columns;
    grid.rows = rows;
    grid.channels = static_cast<std::uint8_t>(lin.size());
    grid.values.assign(cells * lin.size(), 0);
    // Une tâche par rangée de cases : lit les couleurs constantes, écrit ses seules cases.
    parallel_for(static_cast<std::size_t>(rows), [&](std::size_t r) {
        for (std::size_t c = 0; c < static_cast<std::size_t>(columns); ++c) {
            const std::size_t idx = r * static_cast<std::size_t>(columns) + c;
            auto w = mix_with(color[idx], lin, lambda, subsets, nullptr);
            if (minShare > 0.0) {
                double kept = 0.0;
                std::size_t strongest = 0;
                for (std::size_t i = 0; i < w.size(); ++i) {
                    if (w[i] > w[strongest]) {
                        strongest = i;
                    }
                }
                for (auto& v : w) {
                    if (v < minShare) {
                        v = 0.0;
                    }
                    kept += v;
                }
                if (kept <= 0.0) {
                    w[strongest] = 1.0;
                    kept = 1.0;
                }
                for (auto& v : w) {
                    v /= kept;
                }
            }
            quantize_into(w, grid.values.data() + idx * lin.size());
        }
    });
    return grid;
}

Result<std::vector<Rgb8>> propose_blend_palette(const image::Image& image, Millimeters mm_per_px,
                                                const std::vector<geometry::PathSet>& shape,
                                                std::size_t count,
                                                const ImageBlendOptions& options) {
    if (image.empty() || mm_per_px.value <= 0.0) {
        return fail(ErrorCategory::UserInput, "Aucune image à analyser");
    }
    if (count < 2 || count > 8) {
        return fail(ErrorCategory::UserInput, "Un fondu depuis l'image demande de 2 à 8 fils");
    }
    const auto samples = sample_region_colors(image, mm_per_px, shape, 20'000);
    if (samples.size() < count) {
        return fail(ErrorCategory::UserInput, "La forme ne recouvre pas assez de pixels");
    }
    std::vector<V3> px;
    px.reserve(samples.size());
    for (const auto& s : samples) {
        px.push_back(to_linear(s.rgb));
    }
    V3 mean{};
    for (const auto& p : px) {
        for (std::size_t k = 0; k < 3; ++k) {
            mean[k] += p[k] / static_cast<double>(px.size());
        }
    }
    // Premier axe principal : itération de la puissance (départ fixe, signe canonique).
    std::array<std::array<double, 3>, 3> cov{};
    for (const auto& p : px) {
        for (std::size_t a = 0; a < 3; ++a) {
            for (std::size_t b = 0; b < 3; ++b) {
                cov[a][b] += (p[a] - mean[a]) * (p[b] - mean[b]);
            }
        }
    }
    V3 axis{0.577, 0.577, 0.577};
    for (int it = 0; it < 64; ++it) {
        V3 next{};
        for (std::size_t a = 0; a < 3; ++a) {
            for (std::size_t b = 0; b < 3; ++b) {
                next[a] += cov[a][b] * axis[b];
            }
        }
        const double n = std::sqrt(sq(next[0]) + sq(next[1]) + sq(next[2]));
        if (n < 1e-15) {
            break;
        }
        for (std::size_t a = 0; a < 3; ++a) {
            axis[a] = next[a] / n;
        }
    }
    const auto project = [&](const V3& p, const V3& dir) {
        return (p[0] - mean[0]) * dir[0] + (p[1] - mean[1]) * dir[1] + (p[2] - mean[2]) * dir[2];
    };
    // Candidates : k-means (2 × count centres, départ aux quantiles du premier axe) puis
    // extrêmes (1er et 99e centiles) le long de quatre axes de couleur.
    std::vector<std::size_t> order(px.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return project(px[a], axis) < project(px[b], axis);
    });
    const std::size_t kc = std::min(px.size(), 2 * count);
    std::vector<V3> centers;
    for (std::size_t i = 0; i < kc; ++i) {
        centers.push_back(px[order[(2 * i + 1) * px.size() / (2 * kc)]]);
    }
    std::vector<std::size_t> label(px.size(), 0);
    for (int it = 0; it < 30; ++it) {
        for (std::size_t i = 0; i < px.size(); ++i) {
            std::size_t best = 0;
            for (std::size_t c = 1; c < centers.size(); ++c) {
                if (dist2(px[i], centers[c]) < dist2(px[i], centers[best])) {
                    best = c;
                }
            }
            label[i] = best;
        }
        std::vector<V3> acc(centers.size(), V3{});
        std::vector<std::size_t> n(centers.size(), 0);
        for (std::size_t i = 0; i < px.size(); ++i) {
            for (std::size_t k = 0; k < 3; ++k) {
                acc[label[i]][k] += px[i][k];
            }
            ++n[label[i]];
        }
        bool moved = false;
        for (std::size_t c = 0; c < centers.size(); ++c) {
            if (n[c] == 0) {
                continue;
            }
            V3 m{acc[c][0] / n[c], acc[c][1] / n[c], acc[c][2] / n[c]};
            moved = moved || dist2(m, centers[c]) > 1e-14;
            centers[c] = m;
        }
        if (!moved) {
            break;
        }
    }
    std::vector<V3> candidates = centers;
    const std::array<V3, 4> dirs{axis, V3{0.2126, 0.7152, 0.0722}, // luminance
                                 V3{0.7071, -0.7071, 0.0},         // rouge - vert
                                 V3{0.4082, 0.4082, -0.8165}};     // jaune - bleu
    for (const auto& dir : dirs) {
        std::vector<std::size_t> o(px.size());
        std::iota(o.begin(), o.end(), std::size_t{0});
        std::stable_sort(o.begin(), o.end(), [&](std::size_t a, std::size_t b) {
            return project(px[a], dir) < project(px[b], dir);
        });
        candidates.push_back(px[o[px.size() / 100]]);
        candidates.push_back(px[o[px.size() - 1 - px.size() / 100]]);
    }
    // Couleurs d'évaluation : au plus 256 pixels répartis dans l'ordre d'échantillonnage.
    std::vector<V3> eval;
    const std::size_t step = std::max<std::size_t>(1, px.size() / 256);
    for (std::size_t i = 0; i < px.size(); i += step) {
        eval.push_back(px[i]);
    }
    const double lambda = std::isfinite(options.locality) ? std::max(0.0, options.locality) : 0.0;
    const int kmax = static_cast<int>(std::clamp<std::size_t>(options.max_threads_per_point, 2, 4));
    const auto mixError = [&](const std::vector<std::size_t>& picks) {
        std::vector<V3> trial;
        trial.reserve(picks.size());
        for (const auto i : picks) {
            trial.push_back(candidates[i]);
        }
        const auto subsets = supports(static_cast<int>(trial.size()), kmax);
        double error = 0.0;
        for (const auto& v : eval) {
            double residual = 0.0;
            (void)mix_with(v, trial, lambda, subsets, &residual);
            error += residual;
        }
        return error;
    };
    // Départ : la meilleure PAIRE (une couleur moyenne seule ne sert à rien en mélange, les
    // extrêmes comptent), puis ajout glouton.
    std::vector<std::size_t> chosen;
    {
        double bestError = std::numeric_limits<double>::infinity();
        for (std::size_t a = 0; a < candidates.size(); ++a) {
            for (std::size_t b = a + 1; b < candidates.size(); ++b) {
                const double error = mixError({a, b});
                if (error < bestError - 1e-15) {
                    bestError = error;
                    chosen = {a, b};
                }
            }
        }
    }
    while (chosen.size() < count) {
        double bestError = std::numeric_limits<double>::infinity();
        std::size_t bestCandidate = candidates.size();
        for (std::size_t c = 0; c < candidates.size(); ++c) {
            if (std::find(chosen.begin(), chosen.end(), c) != chosen.end()) {
                continue;
            }
            auto trial = chosen;
            trial.push_back(c);
            const double error = mixError(trial);
            if (error < bestError - 1e-15) {
                bestError = error;
                bestCandidate = c;
            }
        }
        if (bestCandidate == candidates.size()) {
            break;
        }
        chosen.push_back(bestCandidate);
    }
    std::vector<Rgb8> out;
    for (const auto i : chosen) {
        out.push_back(to_rgb8(candidates[i]));
    }
    std::stable_sort(out.begin(), out.end(), [](const Rgb8& a, const Rgb8& b) {
        return 0.2126 * a[0] + 0.7152 * a[1] + 0.0722 * a[2] <
               0.2126 * b[0] + 0.7152 * b[1] + 0.0722 * b[2];
    });
    // Deux fils voisins identiques après arrondi : refusé par la palette du fondu.
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.size() < 2) {
        return fail(ErrorCategory::UserInput,
                    "Les couleurs de cette forme sont trop uniformes pour un fondu");
    }
    return out;
}

} // namespace openstitch::autodigitize
