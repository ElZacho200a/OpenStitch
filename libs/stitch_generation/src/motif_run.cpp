// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/motif_run.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::stitch_generation {

namespace {

constexpr Micrometers kFlattenTolerance{50};

using P = MotifPoint;

constexpr std::array<P, 9> kWave{P{0, 0},      P{125, 354},  P{250, 500},  P{375, 354}, P{500, 0},
                                 P{625, -354}, P{750, -500}, P{875, -354}, P{1000, 0}};
constexpr std::array<P, 4> kZigzag{P{0, 0}, P{250, 500}, P{750, -500}, P{1000, 0}};
// Feuille : contour supérieur jusqu'à la pointe, contour inférieur de retour, puis nervure.
constexpr std::array<P, 10> kLeaf{P{0, 0},    P{200, 300},  P{500, 450},  P{800, 300},
                                  P{1000, 0}, P{800, -300}, P{500, -450}, P{200, -300},
                                  P{0, 0},    P{1000, 0}};
constexpr std::array<P, 11> kHeart{P{0, 0},     P{500, -500}, P{850, 100}, P{850, 350},
                                   P{675, 500}, P{500, 350},  P{325, 500}, P{150, 350},
                                   P{150, 100}, P{500, -500}, P{1000, 0}};
constexpr std::array<P, 6> kDiamond{P{0, 0},      P{500, 500}, P{1000, 0},
                                    P{500, -500}, P{0, 0},     P{1000, 0}};
constexpr std::array<P, 7> kScallop{P{0, 0},     P{100, 310}, P{250, 450}, P{500, 500},
                                    P{750, 450}, P{900, 310}, P{1000, 0}};

struct Station {
    double x{};
    double y{};
    double nx{};
    double ny{};
};

} // namespace

std::span<const MotifPoint> motif_pattern(document::MotifId id) {
    using document::MotifId;
    switch (id) {
    case MotifId::Zigzag:
        return kZigzag;
    case MotifId::Leaf:
        return kLeaf;
    case MotifId::Heart:
        return kHeart;
    case MotifId::Diamond:
        return kDiamond;
    case MotifId::Scallop:
        return kScallop;
    case MotifId::Wave:
        break;
    }
    return kWave;
}

std::vector<MotifLocal> motif_local_points(document::MotifId id, double width_um, double height_um,
                                           double max_um) {
    const std::span<const MotifPoint> pat = motif_pattern(id);
    std::vector<MotifLocal> out;
    max_um = std::max(max_um, 1.0);
    for (std::size_t i = 0; i < pat.size(); ++i) {
        const MotifLocal b{pat[i].x * width_um / 1000.0, pat[i].y * height_um / 1000.0};
        if (i > 0) {
            const MotifLocal a = out.back();
            const double len = std::hypot(b.u - a.u, b.v - a.v);
            const int pieces = std::max(1, static_cast<int>(std::ceil(len / max_um)));
            for (int k = 1; k < pieces; ++k) {
                const double f = static_cast<double>(k) / pieces;
                out.push_back({a.u + (b.u - a.u) * f, a.v + (b.v - a.v) * f});
            }
        }
        out.push_back(b);
    }
    return out;
}

std::vector<Vec2um> enforce_min_length(const std::vector<Vec2um>& pts, double min_um) {
    std::vector<Vec2um> out;
    out.reserve(pts.size());
    for (std::size_t i = 0; i < pts.size(); ++i) {
        if (out.empty()) {
            out.push_back(pts[i]);
            continue;
        }
        const double d = std::hypot(double(pts[i].x.value) - double(out.back().x.value),
                                    double(pts[i].y.value) - double(out.back().y.value));
        if (i + 1 == pts.size()) {
            if (d > 0.0 && d < min_um && out.size() > 1) {
                out.back() = pts[i]; // le dernier point est conservé, il remplace son voisin
            } else if (d > 0.0) {
                out.push_back(pts[i]);
            }
        } else if (d >= min_um) {
            out.push_back(pts[i]);
        }
    }
    return out;
}

std::vector<Vec2um> motif_run_points(const geometry::Path& path,
                                     const document::MotifRunParams& params) {
    const geometry::Polyline flat = geometry::flatten(path, kFlattenTolerance);
    std::vector<Vec2um> pts = flat.points;
    const bool closed = path.closed && pts.size() >= 3;
    if (closed && pts.front() != pts.back()) {
        pts.push_back(pts.front());
    }
    if (pts.size() < 2) {
        return {};
    }
    const std::vector<double> cum = geometry::cumulative_lengths(pts);
    const double total = cum.back();
    if (total <= 0.0) {
        return {};
    }
    const double spacing =
        static_cast<double>(std::clamp(params.spacing, kMotifMinSpacing, kMotifMaxSpacing).value);
    const double size =
        static_cast<double>(std::clamp(params.size, kMotifMinSize, kMotifMaxSize).value);
    const double maxStitch = static_cast<double>(
        std::clamp(params.stitch_length, kMotifMinStitch, kMotifMaxStitch).value);
    const double minStitch =
        std::clamp(static_cast<double>(params.min_length.value), 0.0, maxStitch);
    const long long reps = std::max<long long>(1, std::llround(total / spacing));
    const double pitch = total / static_cast<double>(reps);
    const std::vector<MotifLocal> local = motif_local_points(params.motif, pitch, size, maxStitch);

    std::vector<Vec2um> out;
    out.reserve(local.size() * static_cast<std::size_t>(reps));
    std::size_t seg = 0; // les abscisses croissent : le segment courant ne recule jamais
    for (long long k = 0; k < reps; ++k) {
        const bool flip = params.alternate && (k % 2) != 0;
        for (std::size_t i = (k == 0 ? 0 : 1); i < local.size(); ++i) {
            const double s = std::clamp(static_cast<double>(k) * pitch + local[i].u, 0.0, total);
            while (seg + 2 < pts.size() && cum[seg + 1] < s) {
                ++seg;
            }
            const double len = cum[seg + 1] - cum[seg];
            const double dx = double(pts[seg + 1].x.value) - double(pts[seg].x.value);
            const double dy = double(pts[seg + 1].y.value) - double(pts[seg].y.value);
            const double t = len > 0.0 ? std::clamp((s - cum[seg]) / len, 0.0, 1.0) : 0.0;
            const double norm = std::hypot(dx, dy);
            const double nx = norm > 0.0 ? -dy / norm : 0.0;
            const double ny = norm > 0.0 ? dx / norm : 1.0;
            const double v = flip ? -local[i].v : local[i].v;
            const double px = double(pts[seg].x.value) + dx * t + nx * v;
            const double py = double(pts[seg].y.value) + dy * t + ny * v;
            const Vec2um p{Micrometers{static_cast<std::int32_t>(std::llround(px))},
                           Micrometers{static_cast<std::int32_t>(std::llround(py))}};
            if (out.empty() || out.back() != p) {
                out.push_back(p);
            }
        }
    }
    return enforce_min_length(out, minStitch);
}

} // namespace openstitch::stitch_generation
