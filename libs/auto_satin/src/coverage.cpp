// SPDX-License-Identifier: Apache-2.0
#include "coverage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "geometry_detail.hpp"
#include "openstitch/auto_satin/raster.hpp"

namespace openstitch::auto_satin::detail {

namespace {

P2 to_p2(Vec2um v) {
    return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
}

} // namespace

// Couverture estimée : rasterise les triangles balayés par deux traversées
// consécutives d'une même colonne (rendu des fils) sur le masque de la région.
void measure_coverage(const geometry::PathSet& region, SkeletonSatinResult& result) {
    SkeletonRasterParameters raster;
    raster.pixel_size = Micrometers{100};
    const auto mask = rasterize(region, raster);
    if (!mask || mask->width <= 0 || mask->height <= 0) {
        return;
    }
    const RasterMask& m = *mask;
    std::vector<std::uint8_t> count(m.pixels.size(), 0);
    const double pix = m.transform.pixel_size_um;
    const auto fill = [&](P2 a, P2 b, P2 c) {
        const auto toCol = [&](double x) { return (x - m.transform.min_x_um) / pix; };
        const auto toRow = [&](double y) { return (m.transform.max_y_um - y) / pix; };
        const double ax = toCol(a.x), ay = toRow(a.y);
        const double bx = toCol(b.x), by = toRow(b.y);
        const double cx = toCol(c.x), cy = toRow(c.y);
        const double det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::abs(det) < 1e-9) {
            return;
        }
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}))));
        const int x1 = std::min(m.width - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy}))));
        const int y1 = std::min(m.height - 1, static_cast<int>(std::ceil(std::max({ay, by, cy}))));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                // Échantillon à un décalage sous-pixel FIXE et irrégulier : au coin comme au centre
                // du pixel, une arête alignée sur la grille (traversées parallèles à un axe,
                // espacement multiple de 100 µm) passerait exactement par les échantillons et
                // serait comptée par les deux triangles voisins (faux recouvrement), ou par aucun
                // (faux manque).
                const double px = x + 0.4142;
                const double py = y + 0.5772;
                const double l1 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / det;
                const double l2 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / det;
                const double l3 = 1.0 - l1 - l2;
                if (l1 >= 0.0 && l2 >= 0.0 && l3 >= 0.0) {
                    auto& v =
                        count[static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) +
                              static_cast<std::size_t>(x)];
                    if (v < 255) {
                        ++v;
                    }
                }
            }
        }
    };
    for (const auto& column : result.columns) {
        for (std::size_t i = 0; i + 1 < column.crossings.size(); ++i) {
            const P2 a0 = to_p2(column.crossings[i].a);
            const P2 b0 = to_p2(column.crossings[i].b);
            const P2 a1 = to_p2(column.crossings[i + 1].a);
            const P2 b1 = to_p2(column.crossings[i + 1].b);
            fill(a0, b0, b1);
            fill(a0, b1, a1);
        }
    }
    // Mesure sur le masque ÉRODÉ d'un pixel : les pixels du contour sont à moitié
    // couverts par construction (les cordes s'arrêtent sur le bord), ce qui
    // sous-estimerait la couverture d'autant plus que le périmètre est grand.
    std::size_t inside = 0;
    std::size_t covered = 0;
    std::size_t total = 0;
    int peak = 0;
    int gap_x0 = m.width, gap_x1 = -1, gap_y0 = m.height, gap_y1 = -1;
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x < m.width; ++x) {
            if (!m.at(x, y) || !m.at(x - 1, y) || !m.at(x + 1, y) || !m.at(x, y - 1) ||
                !m.at(x, y + 1)) {
                continue;
            }
            const auto v = count[static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) +
                                 static_cast<std::size_t>(x)];
            ++inside;
            covered += v > 0 ? 1 : 0;
            total += v;
            peak = std::max(peak, static_cast<int>(v));
            if (v == 0) {
                gap_x0 = std::min(gap_x0, x);
                gap_x1 = std::max(gap_x1, x);
                gap_y0 = std::min(gap_y0, y);
                gap_y1 = std::max(gap_y1, y);
            }
        }
    }
    auto& d = result.diagnostics;
    if (inside == 0) {
        return;
    }
    d.coverage_measured = true;
    d.coverage_ratio = static_cast<double>(covered) / static_cast<double>(inside);
    d.overlap_ratio = static_cast<double>(total) / static_cast<double>(inside);
    d.uncovered_area_mm2 = static_cast<double>(inside - covered) * pix * pix / 1e6;
    d.overlap_peak = peak;
    if (gap_x1 >= gap_x0) {
        // Boîte des pixels non couverts, en µm (les rangées croissent vers le bas).
        const auto xum = [&](int col) {
            return static_cast<std::int32_t>(std::lround(m.transform.min_x_um + col * pix));
        };
        const auto yum = [&](int row) {
            return static_cast<std::int32_t>(std::lround(m.transform.max_y_um - row * pix));
        };
        d.uncovered_min = Vec2um{Micrometers{xum(gap_x0)}, Micrometers{yum(gap_y1 + 1)}};
        d.uncovered_max = Vec2um{Micrometers{xum(gap_x1 + 1)}, Micrometers{yum(gap_y0)}};
    }
}

} // namespace openstitch::auto_satin::detail
