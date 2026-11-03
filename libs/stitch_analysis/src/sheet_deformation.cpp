// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/sheet_deformation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "openstitch/stitch_generation/pull_map.hpp"

namespace openstitch::stitch_analysis {

namespace {

using Mat3 = std::array<std::array<double, 3>, 3>;
using Mat8 = std::array<std::array<double, 8>, 8>;

std::int32_t round_um(double v) {
    return static_cast<std::int32_t>(std::lround(v));
}

double clamp_finite(double v, double lo, double hi, double fallback) {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

// Matrice de comportement en contraintes planes, repère global (chaîne tournée de `theta`).
Mat3 constitutive(const SheetParams& p) {
    const double e1 = clamp_finite(p.stiffness_warp_n_per_mm, 1e-6, 1e9, 40.0);
    const double e2 = clamp_finite(p.stiffness_weft_n_per_mm, 1e-6, 1e9, 40.0);
    // nu12 * nu21 = nu12^2 * e2 / e1 doit rester < 1 pour une matrice définie positive.
    const double nu12 =
        std::min(clamp_finite(p.poisson_warp_weft, 0.0, 0.49, 0.25), 0.9 * std::sqrt(e1 / e2));
    const double nu21 = nu12 * e2 / e1;
    const double det = 1.0 - nu12 * nu21;
    const double g =
        p.shear_n_per_mm > 0.0 ? p.shear_n_per_mm : std::sqrt(e1 * e2) / (2.0 * (1.0 + nu12));
    const Mat3 d{
        {{e1 / det, nu12 * e2 / det, 0.0}, {nu12 * e2 / det, e2 / det, 0.0}, {0.0, 0.0, g}}};
    const double th = std::isfinite(p.grain_angle_rad) ? p.grain_angle_rad : 0.0;
    const double c = std::cos(th);
    const double s = std::sin(th);
    // Transformation des déformations (cisaillement en ingénieur) : eps_matériau = T eps_global.
    const Mat3 t{
        {{c * c, s * s, c * s}, {s * s, c * c, -c * s}, {-2 * c * s, 2 * c * s, c * c - s * s}}};
    // D_global = T^T D T (symétrique par construction).
    Mat3 dt{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            for (std::size_t k = 0; k < 3; ++k) {
                dt[i][j] += d[i][k] * t[k][j];
            }
        }
    }
    Mat3 out{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            for (std::size_t k = 0; k < 3; ++k) {
                out[i][j] += t[k][i] * dt[k][j];
            }
        }
    }
    return out;
}

// Rigidité d'un élément Q4 carré de côté h (mm), intégration de Gauss 2 x 2. Noeuds dans l'ordre
// (-1,-1) (1,-1) (1,1) (-1,1), degrés de liberté (ux, uy) par noeud.
Mat8 element_stiffness(const Mat3& d, double h) {
    constexpr std::array<double, 4> xi{-1.0, 1.0, 1.0, -1.0};
    constexpr std::array<double, 4> et{-1.0, -1.0, 1.0, 1.0};
    const double g = 1.0 / std::sqrt(3.0);
    const double det_j = (h / 2.0) * (h / 2.0);
    Mat8 k{};
    for (const double gx : {-g, g}) {
        for (const double gy : {-g, g}) {
            std::array<std::array<double, 8>, 3> b{};
            for (std::size_t n = 0; n < 4; ++n) {
                const double dx = xi[n] * (1.0 + et[n] * gy) / (2.0 * h);
                const double dy = et[n] * (1.0 + xi[n] * gx) / (2.0 * h);
                b[0][2 * n] = dx;
                b[1][2 * n + 1] = dy;
                b[2][2 * n] = dy;
                b[2][2 * n + 1] = dx;
            }
            std::array<std::array<double, 8>, 3> db{};
            for (std::size_t i = 0; i < 3; ++i) {
                for (std::size_t j = 0; j < 8; ++j) {
                    for (std::size_t m = 0; m < 3; ++m) {
                        db[i][j] += d[i][m] * b[m][j];
                    }
                }
            }
            for (std::size_t i = 0; i < 8; ++i) {
                for (std::size_t j = 0; j < 8; ++j) {
                    for (std::size_t m = 0; m < 3; ++m) {
                        k[i][j] += b[m][i] * db[m][j] * det_j;
                    }
                }
            }
        }
    }
    return k;
}

struct Grid {
    std::size_t cols{0};
    std::size_t rows{0};
    double h{1.0};           // mm
    double ox{0.0}, oy{0.0}; // origine, mm

    [[nodiscard]] std::size_t nodes_per_row() const { return cols + 1; }
    [[nodiscard]] std::size_t node(std::size_t i, std::size_t j) const {
        return j * (cols + 1) + i;
    }
    [[nodiscard]] bool fixed(std::size_t i, std::size_t j) const {
        return i == 0 || j == 0 || i == cols || j == rows;
    }
};

// Indices des noeuds de l'élément (e = col, row) dans l'ordre local.
std::array<std::size_t, 4> element_nodes(const Grid& g, std::size_t col, std::size_t row) {
    return {g.node(col, row), g.node(col + 1, row), g.node(col + 1, row + 1), g.node(col, row + 1)};
}

struct Segment {
    double ax, ay, bx, by; // mm, relatifs à l'origine du maillage
};

// y = K x sur les degrés de liberté libres (les fixes sont rendus à l'identité).
void apply_operator(const Grid& g, const Mat8& ke, const std::vector<float>& scale,
                    const std::vector<char>& fixed_dof, const std::vector<double>& x,
                    std::vector<double>& y) {
    std::fill(y.begin(), y.end(), 0.0);
    for (std::size_t row = 0; row < g.rows; ++row) {
        for (std::size_t col = 0; col < g.cols; ++col) {
            const auto nodes = element_nodes(g, col, row);
            std::array<double, 8> xe{};
            for (std::size_t n = 0; n < 4; ++n) {
                xe[2 * n] = fixed_dof[2 * nodes[n]] ? 0.0 : x[2 * nodes[n]];
                xe[2 * n + 1] = fixed_dof[2 * nodes[n] + 1] ? 0.0 : x[2 * nodes[n] + 1];
            }
            const double s = scale[row * g.cols + col];
            for (std::size_t i = 0; i < 8; ++i) {
                double acc = 0.0;
                for (std::size_t j = 0; j < 8; ++j) {
                    acc += ke[i][j] * xe[j];
                }
                y[2 * nodes[i / 2] + (i % 2)] += s * acc;
            }
        }
    }
    for (std::size_t i = 0; i < y.size(); ++i) {
        if (fixed_dof[i]) {
            y[i] = x[i];
        }
    }
}

} // namespace

SheetParams sheet_preset(std::string_view material, std::string_view stabilizer) {
    SheetParams p;
    if (material == "jersey") {
        p.stiffness_warp_n_per_mm = 10.0;
        p.stiffness_weft_n_per_mm = 5.0;
        p.poisson_warp_weft = 0.35;
        p.shear_n_per_mm = 2.0;
        p.critical_compressive_strain = 0.004;
    } else if (material == "felt") {
        p.stiffness_warp_n_per_mm = 30.0;
        p.stiffness_weft_n_per_mm = 30.0;
        p.poisson_warp_weft = 0.2;
        p.critical_compressive_strain = 0.003;
    }
    double boost = 1.0;
    if (stabilizer == "tearaway") {
        boost = 1.6;
    } else if (stabilizer == "cutaway") {
        boost = 3.0;
    }
    p.stiffness_warp_n_per_mm *= boost;
    p.stiffness_weft_n_per_mm *= boost;
    if (p.shear_n_per_mm > 0.0) {
        p.shear_n_per_mm *= boost;
    }
    return p;
}

SheetDomain domain_around_stitches(const stitch::StitchSequence& sequence, Micrometers margin) {
    const auto sources = stitch_generation::collect_pull_sources(sequence);
    if (sources.empty()) {
        return {};
    }
    std::int64_t x0 = std::numeric_limits<std::int64_t>::max();
    std::int64_t y0 = x0;
    std::int64_t x1 = std::numeric_limits<std::int64_t>::min();
    std::int64_t y1 = x1;
    for (const auto& s : sources) {
        for (const Vec2um p : {s.a, s.b}) {
            x0 = std::min<std::int64_t>(x0, p.x.value);
            x1 = std::max<std::int64_t>(x1, p.x.value);
            y0 = std::min<std::int64_t>(y0, p.y.value);
            y1 = std::max<std::int64_t>(y1, p.y.value);
        }
    }
    const std::int64_t m = std::max<std::int32_t>(margin.value, 0);
    return {{{static_cast<std::int32_t>(x0 - m)}, {static_cast<std::int32_t>(y0 - m)}},
            {{static_cast<std::int32_t>(x1 + m)}, {static_cast<std::int32_t>(y1 + m)}}};
}

Vec2um SheetDeformation::displacement_at(std::size_t node_col, std::size_t node_row) const {
    const std::size_t i = node_row * node_columns() + node_col;
    return {{round_um(ux_mm[i] * 1000.0)}, {round_um(uy_mm[i] * 1000.0)}};
}

SheetDeformation solve_sheet_deformation(const stitch::StitchSequence& sequence,
                                         const SheetParams& params, const SheetDomain& domain,
                                         Micrometers cell, int max_cells_per_side) {
    SheetDeformation out;
    out.calibrated = params.calibrated;
    const auto sources = stitch_generation::collect_pull_sources(sequence);
    const double span_x = static_cast<double>(domain.max.x.value) - domain.min.x.value;
    const double span_y = static_cast<double>(domain.max.y.value) - domain.min.y.value;
    if (sources.empty() || cell.value <= 0 || span_x <= 0.0 || span_y <= 0.0 ||
        max_cells_per_side < 4) {
        return out;
    }

    double side_um = static_cast<double>(cell.value);
    side_um = std::max(side_um, std::ceil(span_x / max_cells_per_side));
    side_um = std::max(side_um, std::ceil(span_y / max_cells_per_side));
    Grid g;
    g.h = side_um / 1000.0;
    g.cols = std::max<std::size_t>(3, static_cast<std::size_t>(std::ceil(span_x / side_um)));
    g.rows = std::max<std::size_t>(3, static_cast<std::size_t>(std::ceil(span_y / side_um)));
    g.ox = static_cast<double>(domain.min.x.value) / 1000.0;
    g.oy = static_cast<double>(domain.min.y.value) / 1000.0;

    out.origin = domain.min;
    out.cell = Micrometers{round_um(side_um)};
    out.columns = g.cols;
    out.rows = g.rows;
    out.segment_count = sources.size();

    std::vector<Segment> segments;
    segments.reserve(sources.size());
    for (const auto& s : sources) {
        segments.push_back({static_cast<double>(s.a.x.value) / 1000.0 - g.ox,
                            static_cast<double>(s.a.y.value) / 1000.0 - g.oy,
                            static_cast<double>(s.b.x.value) / 1000.0 - g.ox,
                            static_cast<double>(s.b.y.value) / 1000.0 - g.oy});
    }

    // Densité de fil par élément (mm / mm²) : échantillonnage par sous-segments.
    const std::size_t element_count = g.cols * g.rows;
    std::vector<double> length(element_count, 0.0);
    const double step = std::max(0.05, g.h / 2.0);
    for (const Segment& s : segments) {
        const double len = std::hypot(s.bx - s.ax, s.by - s.ay);
        const int n = std::max(1, static_cast<int>(std::ceil(len / step)));
        for (int k = 0; k < n; ++k) {
            const double t = (k + 0.5) / n;
            const double x = s.ax + t * (s.bx - s.ax);
            const double y = s.ay + t * (s.by - s.ay);
            // Dépôt bilinéaire entre centres d'éléments : pas de biais quand le fil longe un bord.
            const double fx = x / g.h - 0.5;
            const double fy = y / g.h - 0.5;
            const double ix = std::floor(fx);
            const double iy = std::floor(fy);
            for (int dj = 0; dj < 2; ++dj) {
                for (int di = 0; di < 2; ++di) {
                    const double wx = di == 0 ? 1.0 - (fx - ix) : fx - ix;
                    const double wy = dj == 0 ? 1.0 - (fy - iy) : fy - iy;
                    const double c = ix + di;
                    const double r = iy + dj;
                    if (c >= 0.0 && r >= 0.0 && c < static_cast<double>(g.cols) &&
                        r < static_cast<double>(g.rows)) {
                        length[static_cast<std::size_t>(r) * g.cols +
                               static_cast<std::size_t>(c)] += wx * wy * len / n;
                    }
                }
            }
        }
    }
    out.thread_density.resize(element_count);
    std::vector<float> scale(element_count);
    const double stiffening = clamp_finite(params.stitch_stiffening, 0.0, 100.0, 0.0);
    for (std::size_t i = 0; i < element_count; ++i) {
        const double density = length[i] / (g.h * g.h);
        out.thread_density[i] = static_cast<float>(density);
        scale[i] = static_cast<float>(1.0 + stiffening * density);
    }

    const Mat3 d = constitutive(params);
    const Mat8 ke = element_stiffness(d, g.h);

    const std::size_t node_count = (g.cols + 1) * (g.rows + 1);
    std::vector<char> fixed_dof(2 * node_count, 0);
    for (std::size_t j = 0; j <= g.rows; ++j) {
        for (std::size_t i = 0; i <= g.cols; ++i) {
            if (g.fixed(i, j)) {
                fixed_dof[2 * g.node(i, j)] = 1;
                fixed_dof[2 * g.node(i, j) + 1] = 1;
            }
        }
    }

    // Charges : dipôle de tension, étalé en gaussienne autour de chaque pénétration.
    std::vector<double> f(2 * node_count, 0.0);
    const double tension = clamp_finite(params.thread_tension_n, 0.0, 1e6, 0.0);
    const double sigma =
        std::max(clamp_finite(params.load_radius_mm, 0.0, 100.0, 0.6) / 2.0, 0.5 * g.h);
    // (2 reach + 1)^2 noeuds au plus : borné par la taille du tampon local (19 x 19 = 361 <= 400).
    const int reach = std::clamp(static_cast<int>(std::ceil(2.0 * sigma / g.h)), 1, 9);
    const auto spread = [&](double px, double py, double fx, double fy) {
        if (px < 0.0 || py < 0.0 || px > g.cols * g.h || py > g.rows * g.h) {
            return;
        }
        // Fenêtre centrée sur le noeud le plus proche (symétrique : pas de biais de maillage).
        const auto ci = static_cast<std::int64_t>(std::llround(px / g.h));
        const auto cj = static_cast<std::int64_t>(std::llround(py / g.h));
        std::array<double, 400> w{};
        std::array<std::size_t, 400> idx{};
        std::size_t count = 0;
        double sum = 0.0;
        for (std::int64_t j = cj - reach; j <= cj + reach; ++j) {
            for (std::int64_t i = ci - reach; i <= ci + reach; ++i) {
                if (i < 0 || j < 0 || i > static_cast<std::int64_t>(g.cols) ||
                    j > static_cast<std::int64_t>(g.rows) || count >= w.size()) {
                    continue;
                }
                const double dx = i * g.h - px;
                const double dy = j * g.h - py;
                const double wt = std::exp(-(dx * dx + dy * dy) / (2.0 * sigma * sigma));
                w[count] = wt;
                idx[count] = g.node(static_cast<std::size_t>(i), static_cast<std::size_t>(j));
                sum += wt;
                ++count;
            }
        }
        if (sum <= 0.0) {
            return;
        }
        for (std::size_t k = 0; k < count; ++k) {
            f[2 * idx[k]] += fx * w[k] / sum;
            f[2 * idx[k] + 1] += fy * w[k] / sum;
        }
    };
    for (const Segment& s : segments) {
        const double len = std::hypot(s.bx - s.ax, s.by - s.ay);
        if (len <= 0.0) {
            continue;
        }
        const double ux = (s.bx - s.ax) / len;
        const double uy = (s.by - s.ay) / len;
        spread(s.ax, s.ay, tension * ux, tension * uy);
        spread(s.bx, s.by, -tension * ux, -tension * uy);
    }
    for (std::size_t i = 0; i < f.size(); ++i) {
        if (fixed_dof[i]) {
            f[i] = 0.0;
        }
    }

    // Gradient conjugué préconditionné (Jacobi).
    std::vector<double> diag(2 * node_count, 0.0);
    for (std::size_t row = 0; row < g.rows; ++row) {
        for (std::size_t col = 0; col < g.cols; ++col) {
            const auto nodes = element_nodes(g, col, row);
            const double s = scale[row * g.cols + col];
            for (std::size_t i = 0; i < 8; ++i) {
                diag[2 * nodes[i / 2] + (i % 2)] += s * ke[i][i];
            }
        }
    }
    for (std::size_t i = 0; i < diag.size(); ++i) {
        if (fixed_dof[i] || diag[i] <= 0.0) {
            diag[i] = 1.0;
        }
    }
    std::vector<double> u(f.size(), 0.0);
    std::vector<double> r = f;
    std::vector<double> z(f.size());
    std::vector<double> p(f.size());
    std::vector<double> q(f.size());
    const auto dot = [](const std::vector<double>& a, const std::vector<double>& b) {
        double acc = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            acc += a[i] * b[i];
        }
        return acc;
    };
    const double f_norm = std::sqrt(dot(f, f));
    if (f_norm > 0.0) {
        for (std::size_t i = 0; i < z.size(); ++i) {
            z[i] = r[i] / diag[i];
        }
        p = z;
        double rz = dot(r, z);
        const int max_it = std::max(1, params.max_iterations);
        const double tol = clamp_finite(params.tolerance, 1e-14, 1e-2, 1e-8);
        double res = f_norm;
        int it = 0;
        while (it < max_it && res > tol * f_norm) {
            apply_operator(g, ke, scale, fixed_dof, p, q);
            const double pq = dot(p, q);
            if (pq <= 0.0) {
                break;
            }
            const double alpha = rz / pq;
            for (std::size_t i = 0; i < u.size(); ++i) {
                u[i] += alpha * p[i];
                r[i] -= alpha * q[i];
            }
            res = std::sqrt(dot(r, r));
            for (std::size_t i = 0; i < z.size(); ++i) {
                z[i] = r[i] / diag[i];
            }
            const double rz_next = dot(r, z);
            const double beta = rz_next / rz;
            rz = rz_next;
            for (std::size_t i = 0; i < p.size(); ++i) {
                p[i] = z[i] + beta * p[i];
            }
            ++it;
        }
        out.iterations = it;
        out.residual = res / f_norm;
        out.converged = res <= tol * f_norm;
    } else {
        out.converged = true;
    }

    out.ux_mm.resize(node_count);
    out.uy_mm.resize(node_count);
    for (std::size_t n = 0; n < node_count; ++n) {
        out.ux_mm[n] = u[2 * n];
        out.uy_mm[n] = u[2 * n + 1];
        const double mag = std::hypot(u[2 * n], u[2 * n + 1]);
        if (mag > out.max_displacement_mm) {
            out.max_displacement_mm = mag;
            const std::size_t i = n % (g.cols + 1);
            const std::size_t j = n / (g.cols + 1);
            out.max_displacement_at = {
                {domain.min.x.value + round_um(static_cast<double>(i) * side_um)},
                {domain.min.y.value + round_um(static_cast<double>(j) * side_um)}};
        }
    }

    // Déformations au centre de chaque élément -> compression principale maximale.
    const double eps_cr = clamp_finite(params.critical_compressive_strain, 1e-6, 1.0, 0.002);
    out.compression.assign(element_count, 0.0F);
    out.wrinkle_index.assign(element_count, 0.0F);
    std::vector<float> positive;
    for (std::size_t row = 0; row < g.rows; ++row) {
        for (std::size_t col = 0; col < g.cols; ++col) {
            const auto nodes = element_nodes(g, col, row);
            constexpr std::array<double, 4> sx{-1.0, 1.0, 1.0, -1.0};
            constexpr std::array<double, 4> sy{-1.0, -1.0, 1.0, 1.0};
            double ex = 0.0;
            double ey = 0.0;
            double gxy = 0.0;
            for (std::size_t n = 0; n < 4; ++n) {
                const double dx = sx[n] / (2.0 * g.h);
                const double dy = sy[n] / (2.0 * g.h);
                ex += dx * u[2 * nodes[n]];
                ey += dy * u[2 * nodes[n] + 1];
                gxy += dy * u[2 * nodes[n]] + dx * u[2 * nodes[n] + 1];
            }
            const double mean = 0.5 * (ex + ey);
            const double rad = std::hypot(0.5 * (ex - ey), 0.5 * gxy);
            const double compression = std::max(0.0, rad - mean); // -eps_min
            const std::size_t e = row * g.cols + col;
            out.compression[e] = static_cast<float>(compression);
            const double index = compression / eps_cr;
            out.wrinkle_index[e] = static_cast<float>(index);
            if (index > 0.0) {
                positive.push_back(out.wrinkle_index[e]);
            }
            if (index >= 1.0) {
                out.wrinkle_area_mm2 += g.h * g.h;
            }
            if (index > out.max_wrinkle_index) {
                out.max_wrinkle_index = index;
                out.max_wrinkle_at = {
                    {domain.min.x.value + round_um((static_cast<double>(col) + 0.5) * side_um)},
                    {domain.min.y.value + round_um((static_cast<double>(row) + 0.5) * side_um)}};
            }
        }
    }
    if (!positive.empty()) {
        std::sort(positive.begin(), positive.end());
        out.p99_wrinkle_index = positive[static_cast<std::size_t>(0.99 * (positive.size() - 1))];
    }
    return out;
}

SheetRaster rasterize_wrinkle_index(const SheetDeformation& deformation, double scale) {
    SheetRaster out;
    if (deformation.empty()) {
        return out;
    }
    if (!(scale > 0.0)) {
        scale = 1.0;
    }
    out.width = static_cast<int>(deformation.columns);
    out.height = static_cast<int>(deformation.rows);
    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
    constexpr std::array<std::array<double, 3>, 4> palette = {{{{60.0, 135.0, 230.0}},
                                                               {{65.0, 195.0, 120.0}},
                                                               {{250.0, 215.0, 65.0}},
                                                               {{215.0, 45.0, 45.0}}}};
    for (int y = 0; y < out.height; ++y) {
        // Ligne 0 = haut : élément de rang le plus élevé.
        const std::size_t row = deformation.rows - 1 - static_cast<std::size_t>(y);
        for (int x = 0; x < out.width; ++x) {
            const std::size_t e = row * deformation.columns + static_cast<std::size_t>(x);
            const double v = deformation.wrinkle_index[e];
            if (v <= 0.0 && deformation.thread_density[e] <= 0.0F) {
                continue;
            }
            const double pos = std::clamp(v / scale * 3.0, 0.0, 3.0);
            const int lo = std::min(2, static_cast<int>(pos));
            const double t = pos - lo;
            std::uint8_t* dst = out.rgba.data() + (static_cast<std::size_t>(y) * out.width + x) * 4;
            for (std::size_t ch = 0; ch < 3; ++ch) {
                dst[ch] = static_cast<std::uint8_t>(
                    palette[static_cast<std::size_t>(lo)][ch] * (1.0 - t) +
                    palette[static_cast<std::size_t>(lo) + 1][ch] * t);
            }
            dst[3] = v > 0.0 ? 200 : 60;
        }
    }
    return out;
}

} // namespace openstitch::stitch_analysis
