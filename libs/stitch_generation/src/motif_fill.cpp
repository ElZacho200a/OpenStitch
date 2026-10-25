// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/motif_fill.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/stitch_generation/motif_run.hpp"

namespace openstitch::stitch_generation {

namespace {

// Nombre maximal d'instances d'une région : garde-fou contre un pas minuscule sur une grande zone.
constexpr long long kMaxInstances = 400'000;

bool ring_contains(const geometry::Path& ring, double x, double y) {
    bool inside = false;
    const std::size_t n = ring.nodes.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = ring.nodes[i].pos.x.value;
        const double yi = ring.nodes[i].pos.y.value;
        const double xj = ring.nodes[j].pos.x.value;
        const double yj = ring.nodes[j].pos.y.value;
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

bool region_contains(const geometry::PathSet& region, double x, double y) {
    if (region.outer.nodes.size() < 3 || !ring_contains(region.outer, x, y)) {
        return false;
    }
    for (const geometry::Path& hole : region.holes) {
        if (hole.nodes.size() >= 3 && ring_contains(hole, x, y)) {
            return false;
        }
    }
    return true;
}

Vec2um to_um(double x, double y) {
    return {Micrometers{static_cast<std::int32_t>(std::llround(x))},
            Micrometers{static_cast<std::int32_t>(std::llround(y))}};
}

} // namespace

std::vector<FillStitch> fill_motif(const geometry::PathSet& region,
                                   const document::MotifFillParams& params) {
    std::vector<FillStitch> out;
    if (region.outer.nodes.size() < 3) {
        return out;
    }
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const geometry::PathNode& n : region.outer.nodes) {
        x0 = std::min(x0, double(n.pos.x.value));
        y0 = std::min(y0, double(n.pos.y.value));
        x1 = std::max(x1, double(n.pos.x.value));
        y1 = std::max(y1, double(n.pos.y.value));
    }
    const double size =
        static_cast<double>(std::clamp(params.size, kMotifMinSize, kMotifMaxSize).value);
    const double spacing =
        static_cast<double>(std::clamp(params.spacing, kMotifMinSpacing, kMotifMaxSpacing).value);
    const double maxStitch = static_cast<double>(
        std::clamp(params.stitch_length, kMotifMinStitch, kMotifMaxStitch).value);
    const double minStitch =
        std::clamp(static_cast<double>(params.min_length.value), 0.0, maxStitch);

    const std::vector<MotifLocal> local = motif_local_points(params.motif, size, size, maxStitch);
    const double cx = (x0 + x1) / 2.0;
    const double cy = (y0 + y1) / 2.0;
    const double ca = std::cos(params.angle.radians);
    const double sa = std::sin(params.angle.radians);
    // Demi-diagonale de la boîte : la grille tournée couvre tout le disque englobant.
    const double radius = std::hypot(x1 - x0, y1 - y0) / 2.0 + size;
    const long long half = static_cast<long long>(std::ceil(radius / spacing));
    if ((2 * half + 1) * (2 * half + 1) > kMaxInstances) {
        return out;
    }

    std::vector<std::vector<Vec2um>> strokes;
    for (long long j = -half; j <= half; ++j) {
        const bool odd = ((j % 2) + 2) % 2 == 1;
        const bool reverse = ((j % 2) + 2) % 2 == 1; // serpentin
        for (long long ii = -half; ii <= half; ++ii) {
            const long long i = reverse ? half - (ii + half) : ii;
            const double a =
                (static_cast<double>(i) + (params.stagger && odd ? 0.5 : 0.0)) * spacing;
            const double b = static_cast<double>(j) * spacing;
            const double gx = cx + ca * a - sa * b;
            const double gy = cy + sa * a + ca * b;
            if (gx < x0 - size || gx > x1 + size || gy < y0 - size || gy > y1 + size) {
                continue;
            }
            std::vector<Vec2um> run;
            const auto flush = [&] {
                if (run.size() >= 2) {
                    auto bounded = enforce_min_length(run, minStitch);
                    if (bounded.size() >= 2) {
                        strokes.push_back(std::move(bounded));
                    }
                }
                run.clear();
            };
            for (const MotifLocal& m : local) {
                const double lu = m.u - size / 2.0;
                const double px = gx + ca * lu - sa * m.v;
                const double py = gy + sa * lu + ca * m.v;
                if (region_contains(region, px, py)) {
                    const Vec2um p = to_um(px, py);
                    if (run.empty() || run.back() != p) {
                        run.push_back(p);
                    }
                } else {
                    flush();
                }
            }
            flush();
        }
    }
    for (const auto& stroke : strokes) {
        for (std::size_t k = 0; k < stroke.size(); ++k) {
            out.push_back({stroke[k], k == 0, false});
        }
    }
    return out;
}

} // namespace openstitch::stitch_generation
