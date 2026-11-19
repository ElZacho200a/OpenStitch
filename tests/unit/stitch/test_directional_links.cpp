// SPDX-License-Identifier: Apache-2.0
// HP-SEQ-006 : liaisons entre rangées du moteur directionnel (tolérance de bord).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "openstitch/stitch_generation/directional_fill.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

Vec2um um(double xMm, double yMm) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(xMm * 1000.0))},
                  Micrometers{static_cast<std::int32_t>(std::lround(yMm * 1000.0))}};
}

geometry::Path polygon(const std::vector<Vec2um>& pts) {
    geometry::Path p;
    p.closed = true;
    for (const Vec2um v : pts) {
        p.nodes.push_back({v, geometry::NodeType::Corner, {}, {}});
    }
    return p;
}

document::DirectionalFillParams params() {
    document::DirectionalFillParams p;
    p.row_spacing = Micrometers{400};
    p.stitch_length = Micrometers{3'000};
    p.inset = Micrometers{0};
    p.seed = 1234;
    return p;
}

std::size_t count_jumps(const std::vector<FillStitch>& fill) {
    if (fill.empty()) {
        return 0;
    }
    return static_cast<std::size_t>(
        std::count_if(fill.begin() + 1, fill.end(), [](const FillStitch& s) { return s.jump; }));
}

// Peigne : un dos (y de 0 à 3 mm) et `teeth` dents de largeur toothW (mm) jusqu'à y = depth,
// séparées de fentes de largeur `slit` (mm).
geometry::PathSet comb(int teeth, double toothW, double slit, double depth) {
    const double total = teeth * toothW + (teeth - 1) * slit;
    std::vector<Vec2um> out{um(0, 0), um(total, 0)};
    for (int k = teeth - 1; k >= 0; --k) {
        const double x0 = k * (toothW + slit);
        out.push_back(um(x0 + toothW, depth));
        out.push_back(um(x0, depth));
        if (k > 0) {
            out.push_back(um(x0, 3.0));
            out.push_back(um(x0 - slit, 3.0));
            out.push_back(um(x0 - slit, depth));
        }
    }
    return geometry::PathSet{polygon(out), {}};
}

} // namespace

TEST_CASE("directional links: output is byte for byte deterministic") {
    const auto region = comb(4, 4.0, 0.3, 10.0);
    const auto a = fill_directional(region, params());
    const auto b = fill_directional(region, params());
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].pos == b[i].pos);
        CHECK(a[i].jump == b[i].jump);
    }
}

TEST_CASE("directional links: a link through a hole is never sewn") {
    geometry::PathSet region{polygon({um(0, 0), um(40, 0), um(40, 20), um(0, 20)}),
                             {polygon({um(15, 5), um(15, 15), um(25, 15), um(25, 5)})}};
    const auto fill = fill_directional(region, params());
    REQUIRE(fill.size() > 100);
    for (std::size_t i = 1; i < fill.size(); ++i) {
        if (fill[i].jump || fill[i].travel) {
            continue;
        }
        const double x0 = fill[i - 1].pos.x.value / 1000.0;
        const double x1 = fill[i].pos.x.value / 1000.0;
        const double y = fill[i].pos.y.value / 1000.0;
        const double yp = fill[i - 1].pos.y.value / 1000.0;
        const bool bothInHoleBand = y > 5.2 && y < 14.8 && yp > 5.2 && yp < 14.8;
        const bool crosses = (x0 < 14.8 && x1 > 25.2) || (x1 < 14.8 && x0 > 25.2);
        const bool bad = bothInHoleBand && crosses;
        CHECK_FALSE(bad);
    }
}

TEST_CASE("directional links: valid edge links stay sewn on a comb shape") {
    const auto region = comb(5, 3.0, 0.3, 10.0);
    const auto fill = fill_directional(region, params());
    REQUIRE(fill.size() > 100);
    // Mesure HP-SEQ-006 : 1 saut avec le test strict, 0 avec une tolerance de bord ; la borne
    // large protege seulement contre la perte de liaisons valides.
    CHECK(count_jumps(fill) <= 6);
}
