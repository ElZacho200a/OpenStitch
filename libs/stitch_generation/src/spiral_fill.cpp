// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/spiral_fill.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "fill_geometry.hpp"

namespace openstitch::stitch_generation {

namespace {

constexpr double kMaxStepRad = 0.35;
constexpr std::size_t kMaxSamples = 3'000'000; // garde-fou (région démesurée, espacement mini)
constexpr double kMinSegmentUm = 100.0;

} // namespace

std::vector<FillStitch> fill_spiral(const geometry::PathSet& region,
                                    const document::SpiralFillParams& params) {
    using namespace detail;
    const double spacing = std::clamp(double(params.row_spacing.value), 100.0, 10'000.0);
    const double maxLen = std::clamp(double(params.stitch_length.value), 1'000.0, 7'000.0);
    const double minLen = std::clamp(double(params.min_length.value), 0.0, maxLen);

    const Polar frame = polar_frame(region, params.center);
    if (frame.radius <= 1.0 || region.outer.nodes.size() < 3) {
        return {};
    }
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    const double b = spacing / kTwoPi; // r = b·θ
    const double dir = params.clockwise ? -1.0 : 1.0;
    const auto at = [&](double theta) {
        const double r = b * theta;
        return P2{frame.center.x + r * std::cos(dir * theta),
                  frame.center.y + r * std::sin(dir * theta)};
    };

    FillEmitter emit(region, maxLen, 0.0);
    double theta = 0.0;
    P2 prev = at(0.0);
    P2 lastSewn = prev; // dernier point émis le long de la spirale (pour `min_length`)
    bool open = false;  // la corde précédente se terminait dans la région (pas de rupture)
    std::size_t samples = 0;
    while (b * theta <= frame.radius && samples++ < kMaxSamples) {
        const double r = b * theta;
        const double step = std::min(maxLen / std::hypot(std::max(r, b), b), kMaxStepRad);
        const double nextTheta = theta + step;
        P2 next = at(nextTheta);
        const bool last = b * nextTheta > frame.radius;
        // Points trop proches du dernier point cousu : la corde est repoussée (sauf dernier).
        if (!last && std::hypot(next.x - lastSewn.x, next.y - lastSewn.y) < minLen) {
            theta = nextTheta;
            continue;
        }
        const auto spans = inside_intervals(region, prev, next);
        if (spans.empty()) {
            open = false;
        }
        for (const auto& [t0, t1] : spans) {
            const P2 a = lerp(prev, next, t0);
            const P2 e = lerp(prev, next, t1);
            if (std::hypot(e.x - a.x, e.y - a.y) < kMinSegmentUm && !(open && t0 <= 1e-9)) {
                open = false; // éclat trop court : ignoré
                continue;
            }
            if (!(open && t0 <= 1e-9)) {
                emit.start_at(a);
            }
            emit.line_to(e);
            open = t1 >= 1.0 - 1e-9;
        }
        lastSewn = next;
        prev = next;
        theta = nextTheta;
    }
    return emit.take();
}

} // namespace openstitch::stitch_generation
