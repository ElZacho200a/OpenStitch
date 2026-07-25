// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "openstitch/geometry/polyline.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

PathNode corner(std::int32_t x, std::int32_t y) {
    return {Vec2um{Micrometers{x}, Micrometers{y}}, NodeType::Corner, std::nullopt, std::nullopt};
}

}  // namespace

TEST_CASE("flatten : polyligne droite inchangee") {
    Path p;
    p.closed = false;
    p.nodes = {corner(0, 0), corner(10'000, 0)};
    const Polyline fl = flatten(p, Micrometers{100});
    REQUIRE(fl.points.size() == 2);
    CHECK(fl.points.front() == Vec2um{Micrometers{0}, Micrometers{0}});
    CHECK(fl.points.back() == Vec2um{Micrometers{10'000}, Micrometers{0}});
}

TEST_CASE("flatten : Bezier subdivisee, erreur bornee et longueur coherente") {
    // Quart de cercle approx. par une Bezier cubique (rayon 10 mm).
    // Controle magique kappa = 0.5523 pour un quart de cercle.
    const double R = 10'000.0;
    const double k = 0.5523;
    Path p;
    p.closed = false;
    PathNode a = corner(10'000, 0);
    a.tan_out = Vec2um{Micrometers{0}, Micrometers{static_cast<std::int32_t>(k * R)}};
    PathNode b = corner(0, 10'000);
    b.tan_in = Vec2um{Micrometers{static_cast<std::int32_t>(k * R)}, Micrometers{0}};
    p.nodes = {a, b};

    const Polyline fl = flatten(p, Micrometers{50});
    REQUIRE(fl.points.size() > 5);  // reellement subdivisee
    // Tous les points a ~R du centre (0,0).
    for (const Vec2um& pt : fl.points) {
        const double r = std::hypot(static_cast<double>(pt.x.value), static_cast<double>(pt.y.value));
        CHECK(std::abs(r - R) < 120.0);  // < tolerance elargie
    }
    // Longueur proche de l'arc theorique (pi/2 * R).
    const double L = polyline_length(fl.points);
    CHECK(std::abs(L - (std::numbers::pi / 2.0) * R) < 200.0);
}

TEST_CASE("resample_run : espacement equilibre, extremites exactes") {
    // 10 mm, cible 3 mm -> 4 pas de 2,5 mm (pas de residu court).
    std::vector<Vec2um> pts = {{Micrometers{0}, Micrometers{0}},
                               {Micrometers{10'000}, Micrometers{0}}};
    const auto out = resample_run(pts, Micrometers{3'000});
    REQUIRE(out.size() == 5);
    CHECK(out.front().x.value == 0);
    CHECK(out.back().x.value == 10'000);
    for (std::size_t i = 1; i < out.size(); ++i) {
        CHECK(length_um(out[i] - out[i - 1]) == 2'500.0);
    }
}

TEST_CASE("point_at_length : milieu exact") {
    std::vector<Vec2um> pts = {{Micrometers{0}, Micrometers{0}},
                               {Micrometers{4'000}, Micrometers{0}},
                               {Micrometers{4'000}, Micrometers{3'000}}};
    const auto cum = cumulative_lengths(pts);
    CHECK(cum.back() == 7'000.0);
    const Vec2um mid = point_at_length(pts, cum, 4'000.0);
    CHECK(mid == Vec2um{Micrometers{4'000}, Micrometers{0}});
    const Vec2um q = point_at_length(pts, cum, 5'500.0);
    CHECK(q == Vec2um{Micrometers{4'000}, Micrometers{1'500}});
}
