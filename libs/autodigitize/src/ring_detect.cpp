// SPDX-License-Identifier: Apache-2.0
#include "ring_detect.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace openstitch::autodigitize::detail {

namespace {

bool dark_at(const std::vector<char>& dark, int w, int h, double x, double y) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    return ix >= 0 && iy >= 0 && ix < w && iy < h &&
           dark[static_cast<std::size_t>(iy) * w + ix] != 0;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

std::vector<RingSpec> detect_rings(const std::vector<char>& dark, int w, int h,
                                   const RingDetectOptions& options) {
    std::vector<RingSpec> rings;
    if (w <= 0 || h <= 0 || dark.size() != static_cast<std::size_t>(w) * h) {
        return rings;
    }
    std::vector<int> label(dark.size(), 0);
    std::vector<std::size_t> stack;
    int next = 0;
    const auto at = [&](int x, int y) { return static_cast<std::size_t>(y) * w + x; };
    const double minArea =
        std::numbers::pi * 0.25 * options.min_inner_diameter_px * options.min_inner_diameter_px;
    const double maxArea =
        std::numbers::pi * 0.25 * options.max_inner_diameter_px * options.max_inner_diameter_px;
    for (int y0 = 0; y0 < h; ++y0) {
        for (int x0 = 0; x0 < w; ++x0) {
            if (dark[at(x0, y0)] != 0 || label[at(x0, y0)] != 0) {
                continue;
            }
            ++next;
            stack.assign(1, at(x0, y0));
            label[at(x0, y0)] = next;
            double area = 0, sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
            bool border = false;
            while (!stack.empty()) {
                const std::size_t idx = stack.back();
                stack.pop_back();
                const int x = static_cast<int>(idx % w);
                const int y = static_cast<int>(idx / w);
                area += 1;
                const double fx = x + 0.5, fy = y + 0.5;
                sx += fx;
                sy += fy;
                sxx += fx * fx;
                syy += fy * fy;
                sxy += fx * fy;
                border = border || x == 0 || y == 0 || x == w - 1 || y == h - 1;
                constexpr int dx[4] = {1, -1, 0, 0};
                constexpr int dy[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx[k], ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                        continue;
                    }
                    const std::size_t n = at(nx, ny);
                    if (dark[n] == 0 && label[n] == 0) {
                        label[n] = next;
                        stack.push_back(n);
                    }
                }
            }
            if (border || area < minArea || area > maxArea) {
                continue;
            }
            const double cx = sx / area, cy = sy / area;
            const double vxx = sxx / area - cx * cx + 1.0 / 12.0;
            const double vyy = syy / area - cy * cy + 1.0 / 12.0;
            const double vxy = sxy / area - cx * cy;
            const double tr = 0.5 * (vxx + vyy);
            const double dd = std::sqrt(0.25 * (vxx - vyy) * (vxx - vyy) + vxy * vxy);
            const double l1 = tr + dd, l2 = tr - dd;
            if (l2 <= 0.0) {
                continue;
            }
            RingSpec r;
            r.cx = cx;
            r.cy = cy;
            r.a = 2.0 * std::sqrt(l1);
            r.b = 2.0 * std::sqrt(l2);
            r.angle = 0.5 * std::atan2(2.0 * vxy, vxx - vyy);
            if (r.a > 2.2 * r.b || area < 0.8 * std::numbers::pi * r.a * r.b) {
                continue; // trop allongé, ou pas un disque plein (île sombre importante)
            }
            // Épaisseur : rayons sortants depuis le bord de l'ellipse jusqu'à la fin de la bande.
            const double ca = std::cos(r.angle), sa = std::sin(r.angle);
            std::vector<double> widths, starts;
            constexpr int kRays = 72;
            for (int k = 0; k < kRays; ++k) {
                const double t = 2.0 * std::numbers::pi * k / kRays;
                const double ex = r.a * std::cos(t), ey = r.b * std::sin(t);
                const double bx = r.cx + ex * ca - ey * sa;
                const double by = r.cy + ex * sa + ey * ca;
                // Normale sortante de l'ellipse (gradient), normalisée.
                const double gx0 = std::cos(t) / r.a, gy0 = std::sin(t) / r.b;
                double nx = gx0 * ca - gy0 * sa, ny = gx0 * sa + gy0 * ca;
                const double nl = std::hypot(nx, ny);
                nx /= nl;
                ny /= nl;
                double begin = -1.0, finish = -1.0;
                for (double s = -1.0; s <= options.max_width_px + 1.5; s += 0.25) {
                    const bool d = dark_at(dark, w, h, bx + nx * s, by + ny * s);
                    if (begin < 0.0 && d && s <= 1.75) {
                        begin = s;
                    } else if (begin >= 0.0 && !d) {
                        finish = s;
                        break;
                    } else if (begin < 0.0 && s > 1.75) {
                        break;
                    }
                }
                if (begin >= 0.0 && finish > begin && finish - begin <= options.max_width_px) {
                    widths.push_back(finish - begin);
                    starts.push_back(begin);
                }
            }
            if (widths.size() < 0.75 * kRays) {
                continue;
            }
            const double wm = median(widths);
            std::vector<double> dev;
            for (const double v : widths) {
                dev.push_back(std::abs(v - wm));
            }
            if (wm < options.min_width_px || wm > options.max_width_px || median(dev) > 0.3 * wm) {
                continue;
            }
            const double shift = median(starts); // le bord sombre commence un peu au-delà
            r.a += shift;
            r.b += shift;
            r.width = wm;
            rings.push_back(r);
        }
    }
    return rings;
}

void paint_ring_band(const RingSpec& r, const std::vector<char>& dark, int w, int h,
                     std::vector<char>& out) {
    const double ca = std::cos(r.angle), sa = std::sin(r.angle);
    const double ao = r.a + r.width, bo = r.b + r.width;
    const int x0 = std::max(0, static_cast<int>(std::floor(r.cx - ao - 1)));
    const int x1 = std::min(w - 1, static_cast<int>(std::ceil(r.cx + ao + 1)));
    const int y0 = std::max(0, static_cast<int>(std::floor(r.cy - ao - 1)));
    const int y1 = std::min(h - 1, static_cast<int>(std::ceil(r.cy + ao + 1)));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double dx = x + 0.5 - r.cx, dy = y + 0.5 - r.cy;
            const double u = dx * ca + dy * sa, v = -dx * sa + dy * ca;
            const double rin = std::hypot(u / r.a, v / r.b);
            const double rout = std::hypot(u / ao, v / bo);
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            if (rin >= 1.0 && rout <= 1.0 && dark[i] != 0) {
                out[i] = 1;
            }
        }
    }
}

} // namespace openstitch::autodigitize::detail
