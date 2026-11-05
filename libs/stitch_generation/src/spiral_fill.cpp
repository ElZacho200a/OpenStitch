// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/spiral_fill.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>
#include <utility>
#include <vector>

#include "fill_geometry.hpp"
#include "pull_grow.hpp"

namespace openstitch::stitch_generation {

namespace {

constexpr double kMaxStepRad = 0.35;
constexpr std::size_t kMaxSamples = 3'000'000; // garde-fou (région démesurée, espacement mini)
constexpr double kMinSegmentUm = 100.0;

constexpr std::size_t kMaxRoutedRuns = 2'048;
constexpr double kMaxBridgeUm = 5'000.0; // Limiter les traversées cousues, surtout sur tissu fin.

// Réordonne les portions déjà rognées de la spirale, sans modifier leurs trajets internes.
// Une liaison courte est cousue seulement si le segment complet reste dans la région ;
// sinon elle reste un saut. L'ordre angulaire de construction n'impose pas l'ordre de couture.
std::vector<FillStitch> route_spiral_fragments(const geometry::PathSet& region, double maxLen,
                                                std::vector<FillStitch> raw) {
    struct Run {
        std::size_t begin;
        std::size_t end; // exclusif
    };
    std::vector<Run> runs;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (i == 0 || raw[i].jump) {
            runs.push_back({i, i + 1});
        } else {
            runs.back().end = i + 1;
        }
    }
    // Une spirale convexe est déjà continue ; pas de bruit ni de coût quadratique.
    if (runs.size() < 4 || runs.size() > kMaxRoutedRuns) {
        return raw;
    }

    struct Choice {
        double distance2 = std::numeric_limits<double>::infinity();
        std::size_t run = 0;
        bool reverse = false;
        bool found = false;
    };
    const auto distance2 = [](Vec2um a, Vec2um b) {
        const double dx = double(a.x.value) - double(b.x.value);
        const double dy = double(a.y.value) - double(b.y.value);
        return dx * dx + dy * dy;
    };
    const auto point = [](Vec2um v) {
        return detail::P2{double(v.x.value), double(v.y.value)};
    };
    const double bridge = std::min(kMaxBridgeUm, 2.0 * maxLen);
    const double bridge2 = bridge * bridge;
    const RegionSegmentTester tester(region);
    detail::FillEmitter output(region, maxLen, 0.0);
    std::vector<bool> used(runs.size(), false);

    const auto append = [&](const Run& run, bool reversed, bool jump) {
        const std::size_t start = reversed ? run.end - 1 : run.begin;
        if (jump) {
            output.jump_to(point(raw[start].pos));
        } else {
            output.start_at(point(raw[start].pos));
        }
        if (reversed) {
            for (std::size_t i = run.end - 1; i-- > run.begin;) {
                output.line_to(point(raw[i].pos));
            }
        } else {
            for (std::size_t i = run.begin + 1; i < run.end; ++i) {
                output.line_to(point(raw[i].pos));
            }
        }
    };

    // Conserver le départ au centre : seules les portions suivantes sont réordonnées.
    append(runs.front(), false, false);
    used.front() = true;
    Vec2um current = raw[runs.front().end - 1].pos;

    for (std::size_t remaining = 1; remaining < runs.size(); ++remaining) {
        Choice nearest;
        Choice inside;
        for (std::size_t j = 1; j < runs.size(); ++j) {
            if (used[j]) {
                continue;
            }
            for (int side = 0; side != 2; ++side) {
                const bool reversed = side == 1;
                const Vec2um target =
                    raw[reversed ? runs[j].end - 1 : runs[j].begin].pos;
                const double d2 = distance2(current, target);
                if (d2 < nearest.distance2) {
                    nearest = {d2, j, reversed, true};
                }
                // Tester l'intérieur uniquement sur les candidats de proximité.
                if (d2 <= bridge2 && d2 < inside.distance2 &&
                    tester.stays_inside(current, target)) {
                    inside = {d2, j, reversed, true};
                }
            }
        }
        const Choice best = inside.found ? inside : nearest;
        if (!best.found) {
            break;
        }
        append(runs[best.run], best.reverse, !inside.found);
        used[best.run] = true;
        current = raw[best.reverse ? runs[best.run].begin : runs[best.run].end - 1].pos;
    }

    std::vector<FillStitch> routed = output.take();
    const auto jumps = [](const std::vector<FillStitch>& stitches) {
        return std::count_if(stitches.begin(), stitches.end(),
                             [](const FillStitch& s) { return s.jump; });
    };
    // Ne jamais dégrader un motif dont le parcours initial serait déjà meilleur.
    return jumps(routed) < jumps(raw) ? std::move(routed) : std::move(raw);
}


} // namespace

std::vector<FillStitch> fill_spiral(const geometry::PathSet& baseRegion,
                                    const document::SpiralFillParams& params) {
    using namespace detail;
    // HP-STI-033 : le contour est agrandi de pull_compensation (0 = region inchangee).
    const geometry::PathSet region = grown_for_pull(baseRegion, params.pull_compensation);
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
    return route_spiral_fragments(region, maxLen, emit.take());
}

} // namespace openstitch::stitch_generation
