// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/motif_fill.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "openstitch/stitch_generation/motif_run.hpp"
#include "pull_grow.hpp"

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

// Hachage entier déterministe (splitmix64) d'une cellule de la grille et d'un canal.
std::uint64_t cell_hash(long long i, long long j, std::uint64_t channel) {
    std::uint64_t z =
        static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ULL ^
        (static_cast<std::uint64_t>(j) + 0x632BE59BD9B4E019ULL) * 0xBF58476D1CE4E5B9ULL ^
        channel * 0x94D049BB133111EBULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// [-0,5 ; 0,5[ depuis les 53 bits de poids fort : identique sur toute plateforme.
double centered_unit(std::uint64_t h) {
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0) - 0.5;
}

Vec2um to_um(double x, double y) {
    return {Micrometers{static_cast<std::int32_t>(std::llround(x))},
            Micrometers{static_cast<std::int32_t>(std::llround(y))}};
}

} // namespace

std::vector<FillStitch> fill_motif(const geometry::PathSet& baseRegion,
                                   const document::MotifFillParams& params) {
    std::vector<FillStitch> out;
    // HP-STI-033 : le contour est agrandi de pull_compensation (0 = region inchangee).
    const geometry::PathSet region = detail::grown_for_pull(baseRegion, params.pull_compensation);
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
    const double scatter =
        std::isfinite(params.scatter) ? std::clamp(params.scatter, 0.0, 1.0) : 0.0;
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
            double gx = cx + ca * a - sa * b;
            double gy = cy + sa * a + ca * b;
            double ia = ca; // orientation de l'instance (grille, puis rotation dispersée)
            double ib = sa;
            if (scatter > 0.0) {
                gx += scatter * spacing * centered_unit(cell_hash(i, j, 1));
                gy += scatter * spacing * centered_unit(cell_hash(i, j, 2));
                const double turn =
                    params.angle.radians +
                    scatter * std::numbers::pi * (centered_unit(cell_hash(i, j, 3)) + 0.5);
                ia = std::cos(turn);
                ib = std::sin(turn);
            }
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
                const double px = gx + ia * lu - ib * m.v;
                const double py = gy + ib * lu + ia * m.v;
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
