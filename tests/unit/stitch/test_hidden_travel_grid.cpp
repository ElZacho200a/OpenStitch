// SPDX-License-Identifier: Apache-2.0
//
// R02 (docs/performance/gpu-compute/) : la grille de recouvrement des ponts caches ne teste plus
// toute la boite englobante de chaque point, ni hypot hors de la zone ambigue du seuil. Ce test
// compare, cellule par cellule, la grille construite a celle de l'algorithme d'origine recopie
// ci-dessous, sur des sequences aleatoires (points longs et obliques, horizontaux, verticaux,
// nuls, sauts, sous-couches) et plusieurs largeurs de fil.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <vector>

#include "hidden_travel.hpp"
#include "openstitch/document/finishing.hpp"
#include "openstitch/stitch/sequence.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

// --- Reference : rasterisation d'origine (avant R02), ne pas modifier. ---
double reference_dist_point_segment(double px, double py, double ax, double ay, double bx,
                                    double by) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

std::vector<std::int32_t> reference_grid(const std::vector<stitch::StitchCommand>& cmds,
                                         const HiddenTravelPlanner::Grid& g,
                                         std::int32_t coverWidth) {
    std::vector<std::int32_t> last(g.last.size(), -1);
    const double halfW = std::max(1, coverWidth) / 2.0;
    for (std::size_t i = 1; i < cmds.size(); ++i) {
        const auto& c = cmds[i];
        const auto& p = cmds[i - 1];
        if (c.type != stitch::CommandType::Stitch || c.pass != stitch::StitchPass::TopStitch ||
            p.type != stitch::CommandType::Stitch) {
            continue;
        }
        const std::int32_t cell = g.cell;
        const double ax = p.pos.x.value;
        const double ay = p.pos.y.value;
        const double bx = c.pos.x.value;
        const double by = c.pos.y.value;
        const auto idxOf = [&](double v, std::int32_t o) {
            return static_cast<std::int32_t>(std::floor((v - o) / cell));
        };
        const std::int32_t x0 = std::clamp(idxOf(std::min(ax, bx) - halfW, g.ox), 0, g.w - 1);
        const std::int32_t y0 = std::clamp(idxOf(std::min(ay, by) - halfW, g.oy), 0, g.h - 1);
        const std::int32_t x1 = std::clamp(idxOf(std::max(ax, bx) + halfW, g.ox), 0, g.w - 1);
        const std::int32_t y1 = std::clamp(idxOf(std::max(ay, by) + halfW, g.oy), 0, g.h - 1);
        for (std::int32_t iy = y0; iy <= y1; ++iy) {
            for (std::int32_t ix = x0; ix <= x1; ++ix) {
                const double cx = g.ox + (ix + 0.5) * cell;
                const double cy = g.oy + (iy + 0.5) * cell;
                if (reference_dist_point_segment(cx, cy, ax, ay, bx, by) <= halfW) {
                    last[static_cast<std::size_t>(iy) * g.w + ix] = static_cast<std::int32_t>(i);
                }
            }
        }
    }
    return last;
}

std::vector<stitch::StitchCommand> random_commands(std::uint32_t seed, std::size_t count) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> kind(0, 19);
    std::uniform_int_distribution<int> longStep(-12'000, 12'000);
    std::uniform_int_distribution<int> shortStep(-700, 700);
    std::uniform_int_distribution<int> axis(0, 1);
    std::vector<stitch::StitchCommand> cmds;
    Vec2um p{{0}, {0}};
    ObjectId source{};
    source.value = 1;
    for (std::size_t i = 0; i < count; ++i) {
        const int k = kind(rng);
        Vec2um q = p;
        if (k < 6) {
            q = {{p.x.value + longStep(rng)}, {p.y.value + longStep(rng)}}; // long et oblique
        } else if (k < 9) {
            const int d = longStep(rng);
            q = axis(rng) == 0 ? Vec2um{{p.x.value + d}, p.y} : Vec2um{p.x, {p.y.value + d}};
        } else if (k < 10) {
            q = p; // point nul
        } else {
            q = {{p.x.value + shortStep(rng)}, {p.y.value + shortStep(rng)}};
        }
        // Emprise bornee a 120 mm de cote : la grille reste sous sa limite de taille.
        q = {{std::clamp(q.x.value, -60'000, 60'000)}, {std::clamp(q.y.value, -60'000, 60'000)}};
        stitch::StitchCommand c{};
        c.pos = q;
        c.source = source;
        c.type = k == 19 ? stitch::CommandType::Jump : stitch::CommandType::Stitch;
        c.pass = k == 18 ? stitch::StitchPass::Underlay : stitch::StitchPass::TopStitch;
        cmds.push_back(c);
        p = q;
    }
    return cmds;
}

struct ThreadCountOverride {
    explicit ThreadCountOverride(const char* value) {
#ifdef _WIN32
        _putenv_s("OPENSTITCH_THREADS", value);
#else
        setenv("OPENSTITCH_THREADS", value, 1);
#endif
    }
    ~ThreadCountOverride() {
#ifdef _WIN32
        _putenv_s("OPENSTITCH_THREADS", "");
#else
        unsetenv("OPENSTITCH_THREADS");
#endif
    }
    ThreadCountOverride(const ThreadCountOverride&) = delete;
    ThreadCountOverride& operator=(const ThreadCountOverride&) = delete;
};

} // namespace

TEST_CASE("hidden travel grid : identique cellule par cellule a la rasterisation d origine",
          "[hidden_travel][r02]") {
    // Un fil : construction en une bande ; huit fils : bandes de rangees en parallele.
    for (const char* threads : {"1", "8"}) {
        const ThreadCountOverride forced(threads);
        for (const std::uint32_t seed : {3u, 11u, 4096u}) {
            const auto cmds = random_commands(seed, 4'000);
            for (const std::int32_t width : {0, 1, 350, 400, 1'250}) {
                document::SequenceFinishing finishing;
                finishing.hidden_travel.enabled = true;
                finishing.hidden_travel.cover_thread_width = Micrometers{width};
                HiddenTravelPlanner planner(cmds, finishing);
                const HiddenTravelPlanner::Grid& grid = planner.grid_for_test();
                REQUIRE(grid.w > 0);
                REQUIRE(grid.h > 0);
                const auto want = reference_grid(cmds, grid, width);
                REQUIRE(grid.last.size() == want.size());
                std::size_t covered = 0;
                std::size_t mismatches = 0;
                for (std::size_t k = 0; k < want.size(); ++k) {
                    covered += want[k] >= 0 ? 1 : 0;
                    mismatches += grid.last[k] != want[k] ? 1 : 0;
                }
                INFO("fils " << threads << ", graine " << seed << ", largeur " << width
                             << ", cellule " << grid.cell);
                CHECK(covered > 1'000);
                CHECK(mismatches == 0);
            }
        }
    }
}
