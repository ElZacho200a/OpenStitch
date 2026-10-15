// SPDX-License-Identifier: Apache-2.0
// Primitives de rastérisation partagées par les mesures de couverture de
// `stitch_analysis` (usage INTERNE à la lib, jamais exposé).
//
// `project_metrics.cpp` garde sa propre copie de `dist2_to_segment` et de la boucle de
// tracé : l'extraire aurait obligé à modifier ce fichier (hors périmètre du lot). Les
// règles sont volontairement IDENTIQUES : une cellule est couverte si son centre est à
// moins de `coverage_width / 2` d'un segment cousu.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace openstitch::stitch_analysis::raster {

// Distance² d'un point au segment [a, b].
inline double dist2_to_segment(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double qx = ax + t * dx - px;
    const double qy = ay + t * dy - py;
    return qx * qx + qy * qy;
}

// Grille régulière dans le repère du modèle (µm, Y vers le haut) : la cellule (ix, iy)
// a son centre en (x0 + (ix + ½)·cell, y0 + (iy + ½)·cell).
struct Grid {
    double x0{0.0};
    double y0{0.0};
    double cell{250.0};
    int w{0};
    int h{0};

    [[nodiscard]] std::size_t size() const {
        return static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    }
    [[nodiscard]] std::size_t index(int ix, int iy) const {
        return static_cast<std::size_t>(iy) * static_cast<std::size_t>(w) +
               static_cast<std::size_t>(ix);
    }
    [[nodiscard]] double center_x(int ix) const { return x0 + (ix + 0.5) * cell; }
    [[nodiscard]] double center_y(int iy) const { return y0 + (iy + 0.5) * cell; }
};

// Marque dans `mask` les cellules dont le centre est à moins de `r` du segment [a, b].
inline void stamp_segment(const Grid& g, std::vector<char>& mask, double ax, double ay, double bx,
                          double by, double r) {
    const double r2 = r * r;
    const auto lo = [&](double v, double o) {
        return static_cast<int>(std::floor((v - o) / g.cell - 0.5));
    };
    const auto hi = [&](double v, double o) {
        return static_cast<int>(std::ceil((v - o) / g.cell - 0.5));
    };
    const int x0 = std::max(0, lo(std::min(ax, bx) - r, g.x0));
    const int x1 = std::min(g.w - 1, hi(std::max(ax, bx) + r, g.x0));
    const int y0 = std::max(0, lo(std::min(ay, by) - r, g.y0));
    const int y1 = std::min(g.h - 1, hi(std::max(ay, by) + r, g.y0));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            char& c = mask[g.index(x, y)];
            if (c) {
                continue;
            }
            if (dist2_to_segment(g.center_x(x), g.center_y(y), ax, ay, bx, by) <= r2) {
                c = 1;
            }
        }
    }
}

} // namespace openstitch::stitch_analysis::raster
