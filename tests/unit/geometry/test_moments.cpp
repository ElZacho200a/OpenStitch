// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "openstitch/geometry/moments.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

PathNode node(double x, double y) {
    return PathNode{Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                           Micrometers{static_cast<std::int32_t>(std::lround(y))}},
                    NodeType::Corner, std::nullopt, std::nullopt};
}

// Rectangle w x h (µm) centré en (cx, cy), tourné de `deg` degrés.
Path rotated_rect(double w, double h, double deg, double cx = 50'000.0, double cy = 30'000.0) {
    const double a = deg * std::numbers::pi / 180.0;
    const double c = std::cos(a);
    const double s = std::sin(a);
    Path p;
    p.closed = true;
    for (const auto& [u, v] : {std::pair{-w / 2, -h / 2}, std::pair{w / 2, -h / 2},
                               std::pair{w / 2, h / 2}, std::pair{-w / 2, h / 2}}) {
        p.nodes.push_back(node(cx + u * c - v * s, cy + u * s + v * c));
    }
    return p;
}

double deg_of(const PrincipalAxis& ax) {
    return ax.angle.radians * 180.0 / std::numbers::pi;
}

} // namespace

TEST_CASE("principal_axis : rectangle allonge horizontal -> 0 degre") {
    const auto ax = principal_axis({PathSet{rotated_rect(40'000, 10'000, 0.0), {}}});
    REQUIRE(ax.has_value());
    CHECK(deg_of(*ax) == Catch::Approx(0.0).margin(0.01));
    CHECK(ax->anisotropy == Catch::Approx(16.0).epsilon(1e-3)); // (40/10)^2
    CHECK(ax->area_um2 == Catch::Approx(4e8).epsilon(1e-6));
}

TEST_CASE("principal_axis : rectangle tourne de 30 et 120 degres") {
    const auto a30 = principal_axis({PathSet{rotated_rect(40'000, 8'000, 30.0), {}}});
    const auto a120 = principal_axis({PathSet{rotated_rect(40'000, 8'000, 120.0), {}}});
    REQUIRE((a30 && a120));
    CHECK(deg_of(*a30) == Catch::Approx(30.0).margin(0.05));
    CHECK(deg_of(*a120) == Catch::Approx(120.0).margin(0.05));
}

TEST_CASE("principal_axis : carre -> isotrope, sens de parcours indifferent") {
    Path sq = rotated_rect(20'000, 20'000, 10.0);
    const auto ax = principal_axis({PathSet{sq, {}}});
    std::reverse(sq.nodes.begin(), sq.nodes.end());
    const auto rev = principal_axis({PathSet{sq, {}}});
    REQUIRE((ax && rev));
    CHECK(ax->anisotropy == Catch::Approx(1.0).margin(1e-6));
    CHECK(rev->area_um2 == Catch::Approx(ax->area_um2));
}

TEST_CASE("principal_axis : un trou est soustrait de l'aire") {
    PathSet ring{rotated_rect(40'000, 20'000, 0.0), {rotated_rect(10'000, 10'000, 0.0)}};
    const auto ax = principal_axis({ring});
    REQUIRE(ax.has_value());
    CHECK(ax->area_um2 == Catch::Approx(8e8 - 1e8).epsilon(1e-6));
    CHECK(deg_of(*ax) == Catch::Approx(0.0).margin(0.01));
}

TEST_CASE("principal_axis : forme vide -> nullopt") {
    CHECK_FALSE(principal_axis({}).has_value());
}
