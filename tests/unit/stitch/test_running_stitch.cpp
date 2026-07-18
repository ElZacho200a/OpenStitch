// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_generation/running_stitch.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

geometry::Path line(std::int32_t x0, std::int32_t x1) {
    geometry::Path p;
    p.closed = false;
    p.nodes.push_back({Vec2um{Micrometers{x0}, Micrometers{0}}, geometry::NodeType::Corner,
                       std::nullopt, std::nullopt});
    p.nodes.push_back({Vec2um{Micrometers{x1}, Micrometers{0}}, geometry::NodeType::Corner,
                       std::nullopt, std::nullopt});
    return p;
}

}  // namespace

TEST_CASE("interpolation reguliere : 10 mm en pas de 3 mm -> 4 pas de 2,5 mm") {
    const auto pts = sample_path(line(0, 10'000), Micrometers{3'000}, Micrometers{500});
    REQUIRE(pts.size() == 5);
    CHECK(pts[0].x.value == 0);
    CHECK(pts[1].x.value == 2'500);
    CHECK(pts[2].x.value == 5'000);
    CHECK(pts[3].x.value == 7'500);
    CHECK(pts[4].x.value == 10'000);
}

TEST_CASE("segment court : pas de subdivision") {
    const auto pts = sample_path(line(0, 2'000), Micrometers{3'000}, Micrometers{500});
    CHECK(pts.size() == 2);
}

TEST_CASE("les coins sont preserves") {
    geometry::Path l;
    l.closed = false;
    l.nodes.push_back({Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner,
                       std::nullopt, std::nullopt});
    l.nodes.push_back({Vec2um{Micrometers{5'000}, Micrometers{0}}, geometry::NodeType::Corner,
                       std::nullopt, std::nullopt});
    l.nodes.push_back({Vec2um{Micrometers{5'000}, Micrometers{5'000}}, geometry::NodeType::Corner,
                       std::nullopt, std::nullopt});
    const auto pts = sample_path(l, Micrometers{3'000}, Micrometers{500});
    // Le coin (5000, 0) doit etre un point de couture exact.
    CHECK(std::find(pts.begin(), pts.end(), Vec2um{Micrometers{5'000}, Micrometers{0}}) !=
          pts.end());
}

TEST_CASE("chemin ferme : revient au depart") {
    geometry::Path square;
    square.closed = true;
    const std::int32_t s = 10'000;
    square.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{s}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{s}, Micrometers{s}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{0}, Micrometers{s}}, geometry::NodeType::Corner, {}, {}},
    };
    const auto pts = sample_path(square, Micrometers{4'000}, Micrometers{500});
    REQUIRE(pts.size() >= 2);
    CHECK(pts.front() == pts.back());
    // Perimetre 40 mm en pas <= 4 mm : 4 aretes x 3 pas = 12 segments.
    CHECK(pts.size() == 13);
}

TEST_CASE("repeats : aller-retour et point triple") {
    const std::vector<Vec2um> pts = {{Micrometers{0}, Micrometers{0}},
                                     {Micrometers{1'000}, Micrometers{0}},
                                     {Micrometers{2'000}, Micrometers{0}}};
    const auto doubled = apply_repeats(pts, 2);
    REQUIRE(doubled.size() == 5);
    CHECK(doubled.front() == doubled.back());  // termine au depart

    const auto tripled = apply_repeats(pts, 3);
    REQUIRE(tripled.size() == 7);  // 2 segments x 3 + 1
    // Motif avant/arriere/avant sur le premier segment.
    CHECK(tripled[0] == pts[0]);
    CHECK(tripled[1] == pts[1]);
    CHECK(tripled[2] == pts[0]);
    CHECK(tripled[3] == pts[1]);
    CHECK(tripled[4] == pts[2]);
}
