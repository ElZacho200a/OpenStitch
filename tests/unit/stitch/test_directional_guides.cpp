// SPDX-License-Identifier: Apache-2.0
// Guides directionnels déduits du squelette (HP-STI-018.a).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "openstitch/stitch_generation/directional_guides.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

Vec2um um(double xMm, double yMm) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(xMm * 1000.0))},
                  Micrometers{static_cast<std::int32_t>(std::lround(yMm * 1000.0))}};
}

geometry::PathSet polygon(const std::vector<Vec2um>& pts) {
    geometry::PathSet set;
    set.outer.closed = true;
    for (const Vec2um v : pts) {
        set.outer.nodes.push_back({v, geometry::NodeType::Corner, {}, {}});
    }
    return set;
}

geometry::PathSet disc(double radiusMm) {
    std::vector<Vec2um> pts;
    for (int i = 0; i < 64; ++i) {
        const double a = 2.0 * 3.14159265358979323846 * i / 64.0;
        pts.push_back(um(radiusMm * std::cos(a), radiusMm * std::sin(a)));
    }
    return polygon(pts);
}

} // namespace

TEST_CASE("directional guides: long rectangle gives one central guide", "[directional_guides]") {
    const auto region = polygon({um(0, 0), um(40, 0), um(40, 6), um(0, 6)});
    const auto r = directional_guides_from_region(region);
    INFO(r.has_value() ? "ok"
                       : r.error().code + " | " + r.error().message + " | " + r.error().detail);
    REQUIRE(r.has_value());
    REQUIRE_FALSE(r->guides.empty());
    const auto& g = r->guides.front();
    REQUIRE_FALSE(g.closed);
    REQUIRE(g.nodes.size() >= 2);
    // Guide principal quasi horizontal, proche de y = 3 mm, long d'au moins 20 mm.
    double minX = 1e12;
    double maxX = -1e12;
    for (const auto& n : g.nodes) {
        REQUIRE(std::abs(n.pos.y.value - 3000) < 1500);
        minX = std::min(minX, double(n.pos.x.value));
        maxX = std::max(maxX, double(n.pos.x.value));
    }
    REQUIRE(maxX - minX > 20000.0);
}

TEST_CASE("directional guides: every guide point stays inside the region", "[directional_guides]") {
    const auto region = polygon(
        {um(0, 0), um(30, 0), um(30, 5), um(18, 5), um(18, 25), um(12, 25), um(12, 5), um(0, 5)});
    const auto r = directional_guides_from_region(region);
    INFO(r.has_value() ? "ok"
                       : r.error().code + " | " + r.error().message + " | " + r.error().detail);
    REQUIRE(r.has_value());
    REQUIRE(r->guides.size() <= 8);
    for (const auto& g : r->guides) {
        for (const auto& n : g.nodes) {
            REQUIRE(n.pos.x.value >= 0);
            REQUIRE(n.pos.x.value <= 30000);
            REQUIRE(n.pos.y.value >= 0);
            REQUIRE(n.pos.y.value <= 25000);
        }
    }
}

TEST_CASE("directional guides: the same input gives the same guides", "[directional_guides]") {
    const auto region = polygon({um(0, 0), um(40, 0), um(40, 6), um(0, 6)});
    const auto a = directional_guides_from_region(region);
    const auto b = directional_guides_from_region(region);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->guides == b->guides);
}

TEST_CASE("directional guides: max_guides caps the result", "[directional_guides]") {
    const auto region = polygon(
        {um(0, 0), um(30, 0), um(30, 5), um(18, 5), um(18, 25), um(12, 25), um(12, 5), um(0, 5)});
    DirectionalGuideOptions options;
    options.max_guides = 1;
    const auto r = directional_guides_from_region(region, options);
    INFO(r.has_value() ? "ok"
                       : r.error().code + " | " + r.error().message + " | " + r.error().detail);
    REQUIRE(r.has_value());
    REQUIRE(r->guides.size() == 1);
}

TEST_CASE("directional guides: a round shape is refused with a stable code",
          "[directional_guides]") {
    const auto r = directional_guides_from_region(disc(5.0));
    REQUIRE_FALSE(r.has_value());
    INFO(r.error().code + " | " + r.error().message + " | " + r.error().detail);
    REQUIRE(r.error().code == "E_ROUND_SHAPE");
}
