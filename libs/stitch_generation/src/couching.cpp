// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/couching.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace openstitch::stitch_generation {

namespace {

Vec2um at(double x, double y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(y))}};
}

} // namespace

std::vector<FillStitch> couching_pass(const geometry::PathSet& region,
                                      const document::TatamiParams& params) {
    if (params.couching_spacing.value <= 0) {
        return {};
    }
    // Fils de couchage : rangées perpendiculaires au fil posé, espacées de `couching_spacing`,
    // pénétrations tous les `couching_tie_spacing` (mêmes règles de liaison que le tatami).
    document::TatamiParams couch;
    couch.angle = Angle{params.angle.radians + std::numbers::pi / 2.0};
    couch.row_spacing = Micrometers{std::clamp(params.couching_spacing.value, 2'000, 20'000)};
    couch.stitch_length = Micrometers{std::clamp(params.couching_tie_spacing.value, 1'000, 7'000)};
    couch.stagger = 2;
    couch.inset = Micrometers{0};
    couch.hidden_underpath = params.hidden_underpath;
    const std::vector<FillStitch> rows = fill_tatami(region, couch);

    // Attache à chaque pénétration d'un fil de couchage : petit point en travers du fil (donc
    // dans l'axe du fil posé), aller-retour, qui le plaque sur la couche posée.
    const double cx = std::cos(couch.angle.radians);
    const double cy = std::sin(couch.angle.radians);
    const double lx = std::cos(params.angle.radians);
    const double ly = std::sin(params.angle.radians);
    constexpr double kHalf = 600.0; // demi-attache : 0,6 mm, au-dessus du filtre des points courts
    const RegionSegmentTester inside(region);
    std::vector<FillStitch> out;
    out.reserve(rows.size() * 4);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        out.push_back(rows[i]);
        if (rows[i].jump || rows[i].travel) {
            continue;
        }
        // Une pénétration sur un fil de couchage : le point cousu qui la précède ou la suit est
        // parallèle à la rangée (les liaisons de bord entre rangées n'en reçoivent pas).
        const auto alongRow = [&](std::size_t a, std::size_t b) {
            if (rows[b].jump || rows[b].travel) {
                return false;
            }
            const double vx = double(rows[b].pos.x.value - rows[a].pos.x.value);
            const double vy = double(rows[b].pos.y.value - rows[a].pos.y.value);
            const double len = std::hypot(vx, vy);
            return len > 200.0 && std::abs(vx * cy - vy * cx) <= 0.02 * len;
        };
        const bool onRow =
            (i > 0 && alongRow(i - 1, i)) || (i + 1 < rows.size() && alongRow(i, i + 1));
        if (!onRow) {
            continue;
        }
        const double px = rows[i].pos.x.value;
        const double py = rows[i].pos.y.value;
        const Vec2um a = at(px + kHalf * lx, py + kHalf * ly);
        const Vec2um b = at(px - kHalf * lx, py - kHalf * ly);
        if (!inside.stays_inside(a, b)) {
            continue; // près du bord : l'attache sortirait de la forme
        }
        out.push_back({a, false, false});
        out.push_back({b, false, false});
        out.push_back({rows[i].pos, false, false});
    }
    return out;
}

} // namespace openstitch::stitch_generation
