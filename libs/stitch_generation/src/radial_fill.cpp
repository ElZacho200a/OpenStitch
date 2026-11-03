// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/radial_fill.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "fill_geometry.hpp"

namespace openstitch::stitch_generation {

namespace {

constexpr int kMaxLevelBits = 15; // au plus 32 768 rayons
constexpr double kMinSegmentUm = 200.0;

} // namespace

std::vector<FillStitch> fill_radial(const geometry::PathSet& region,
                                    const document::RadialFillParams& params) {
    using namespace detail;
    const double spacing = std::clamp(double(params.row_spacing.value), 100.0, 10'000.0);
    const double maxLen = std::clamp(double(params.stitch_length.value), 1'000.0, 7'000.0);
    const double minLen = std::clamp(double(params.min_length.value), 0.0, maxLen);

    const Polar frame = polar_frame(region, params.center);
    if (frame.radius <= 1.0 || region.outer.nodes.size() < 3) {
        return {};
    }
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    // N = 2^m rayons : l'écart d'arc à la périphérie est <= spacing.
    const double wanted = std::ceil(kTwoPi * frame.radius / spacing);
    int m = 2;
    while (m < kMaxLevelBits && double(std::uint32_t{1} << m) < wanted) {
        ++m;
    }
    const std::uint32_t total = std::uint32_t{1} << m;

    FillEmitter emit(region, maxLen, minLen);
    bool outward = true;
    for (std::uint32_t i = 0; i < total; ++i) {
        // Niveau du rayon : 0 pour i = 0, sinon m - ctz(i) (les rayons de niveau c sont au nombre
        // de 2^c, d'écart angulaire 2π/2^c). Le niveau c démarre au rayon où l'écart de la
        // famille des niveaux <= c atteint `spacing` : 2^(c-1)·spacing / 2π.
        const int level = i == 0 ? 0 : m - std::countr_zero(i);
        const double startRadius =
            level == 0 ? 0.0 : spacing * double(std::uint32_t{1} << (level - 1)) / kTwoPi;
        if (startRadius >= frame.radius) {
            continue;
        }
        const double angle = kTwoPi * double(i) / double(total);
        const P2 dir{std::cos(angle), std::sin(angle)};
        const P2 far{frame.center.x + dir.x * frame.radius, frame.center.y + dir.y * frame.radius};
        auto spans = inside_intervals(region, frame.center, far);
        // Distances le long du rayon, bornées au rayon de départ du niveau.
        std::vector<std::pair<double, double>> runs;
        for (const auto& [t0, t1] : spans) {
            const double s0 = std::max(t0 * frame.radius, startRadius);
            const double s1 = t1 * frame.radius;
            if (s1 - s0 >= kMinSegmentUm) {
                runs.emplace_back(s0, s1);
            }
        }
        if (runs.empty()) {
            continue;
        }
        if (!outward) {
            std::reverse(runs.begin(), runs.end());
        }
        for (const auto& [s0, s1] : runs) {
            const double from = outward ? s0 : s1;
            const double to = outward ? s1 : s0;
            emit.start_at({frame.center.x + dir.x * from, frame.center.y + dir.y * from});
            emit.line_to({frame.center.x + dir.x * to, frame.center.y + dir.y * to});
        }
        outward = !outward;
    }
    return emit.take();
}

} // namespace openstitch::stitch_generation
