// SPDX-License-Identifier: Apache-2.0
// Frontière commune et bande (fondu à la frontière, docs/studio-fondu-frontiere-spec.md §5.1).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/frontier.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

PathSet rect(std::int32_t x0, std::int32_t y0, std::int32_t w, std::int32_t h) {
    const auto node = [](std::int32_t x, std::int32_t y) {
        return PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, NodeType::Corner, {}, {}};
    };
    PathSet set;
    set.outer.closed = true;
    set.outer.nodes = {node(x0, y0), node(x0 + w, y0), node(x0 + w, y0 + h), node(x0, y0 + h)};
    return set;
}

double total_length(const std::vector<Polyline>& lines) {
    double sum = 0.0;
    for (const auto& line : lines) {
        sum += polyline_length(line.points);
    }
    return sum;
}

} // namespace

TEST_CASE("shared_frontier: two touching squares share one straight edge", "[frontier]") {
    const auto a = rect(0, 0, 10'000, 10'000);
    const auto b = rect(10'000, 0, 10'000, 10'000);
    // Tolérance minimale : un bord exactement colinéaire au bord de découpe est ambigu.
    const auto f = shared_frontier(a, b, Micrometers{100}, Micrometers{1'000});
    REQUIRE(f.has_value());
    REQUIRE(f->size() == 1);
    CHECK(total_length(*f) == Catch::Approx(10'200.0).margin(300.0));
    for (const Vec2um p : f->front().points) {
        CHECK(p.x.value >= 9'900);
    }
}

TEST_CASE("shared_frontier: a vectorisation gap is bridged by the tolerance", "[frontier]") {
    const auto a = rect(0, 0, 10'000, 10'000);
    const auto b = rect(10'300, 0, 10'000, 10'000); // jour de 0,3 mm
    const auto f = shared_frontier(a, b, Micrometers{600}, Micrometers{1'000});
    REQUIRE(f.has_value());
    REQUIRE(f->size() == 1);
    // Bord droit de A (10 mm) ; les bouts sont rognés de la tolérance (0,6 mm chacun), ce qui
    // retire les amorces de ~0,3 mm le long des bords haut et bas.
    CHECK(total_length(*f) == Catch::Approx(9'400.0).margin(200.0));
    for (const Vec2um p : f->front().points) {
        CHECK(p.x.value >= 9'900);
    }
}

TEST_CASE("shared_frontier: distant shapes have no frontier", "[frontier]") {
    const auto a = rect(0, 0, 10'000, 10'000);
    const auto b = rect(15'000, 0, 10'000, 10'000);
    const auto f = shared_frontier(a, b, Micrometers{600}, Micrometers{1'000});
    REQUIRE(f.has_value());
    CHECK(f->empty());
}

TEST_CASE("shared_frontier: contact in two pieces gives two polylines, longest first",
          "[frontier]") {
    // B en forme de U ouvert vers la gauche : touche le bord droit de A en deux endroits.
    const auto a = rect(0, 0, 10'000, 10'000);
    const auto bTop = rect(10'000, 6'000, 5'000, 4'000);
    const auto bBottom = rect(10'000, 0, 5'000, 2'000);
    const auto bridge = rect(14'000, 0, 1'000, 10'000);
    const auto u = union_polygons({bTop, bBottom, bridge});
    REQUIRE(u.has_value());
    REQUIRE(u->size() == 1);
    const auto f = shared_frontier(a, u->front(), Micrometers{100}, Micrometers{1'000});
    REQUIRE(f.has_value());
    REQUIRE(f->size() == 2);
    CHECK(polyline_length(f->at(0).points) == Catch::Approx(4'200.0).margin(300.0));
    CHECK(polyline_length(f->at(1).points) == Catch::Approx(2'200.0).margin(300.0));
}

TEST_CASE("shared_frontier: deterministic output", "[frontier]") {
    const auto a = rect(-3'000, 1'000, 12'000, 9'000);
    const auto b = rect(9'200, -2'000, 8'000, 15'000);
    const auto f1 = shared_frontier(a, b, Micrometers{600}, Micrometers{1'000});
    const auto f2 = shared_frontier(a, b, Micrometers{600}, Micrometers{1'000});
    REQUIRE(f1.has_value());
    REQUIRE(f2.has_value());
    REQUIRE(f1->size() == f2->size());
    for (std::size_t i = 0; i < f1->size(); ++i) {
        CHECK(f1->at(i).points == f2->at(i).points);
    }
}

TEST_CASE("polyline_band: stadium area around a straight segment", "[frontier]") {
    Polyline line;
    line.points = {Vec2um{Micrometers{0}, Micrometers{0}},
                   Vec2um{Micrometers{10'000}, Micrometers{0}}};
    const auto band = polyline_band({line}, Micrometers{2'000});
    REQUIRE(band.has_value());
    REQUIRE(band->size() == 1);
    const double expected = 2.0 * 2'000.0 * 10'000.0 + std::numbers::pi * 2'000.0 * 2'000.0;
    CHECK(path_set_area_um2(band->front()) == Catch::Approx(expected).epsilon(0.02));
    CHECK(polyline_band({}, Micrometers{2'000})->empty());
    CHECK(polyline_band({line}, Micrometers{0})->empty());
}
