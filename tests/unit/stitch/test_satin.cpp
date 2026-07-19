// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/stitch_generation/satin.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

geometry::Path open_path(std::initializer_list<std::pair<std::int32_t, std::int32_t>> pts) {
    geometry::Path p;
    p.closed = false;
    for (const auto& [x, y] : pts) {
        p.nodes.push_back(
            {Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner, {}, {}});
    }
    return p;
}

}  // namespace

TEST_CASE("satin : colonne droite, zigzag entre les deux rails") {
    // Deux rails horizontaux, longueur 20 mm, ecartes de 4 mm.
    const auto railA = open_path({{0, 0}, {20'000, 0}});
    const auto railB = open_path({{0, 4'000}, {20'000, 4'000}});
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};  // 20 mm / 1 mm -> ~20 crossings
    const auto result = fill_satin(railA, railB, cfg);

    CHECK(result.max_width_um == 4'000.0);
    // 21 crossings x 2 points = 42 points de satin.
    CHECK(result.satin.size() == 42);
    // Chaque paire de points relie les deux bords (y=0 et y=4000).
    for (const Vec2um& p : result.satin) {
        CHECK((p.y.value == 0 || p.y.value == 4'000));
    }
}

TEST_CASE("satin : la densite controle le nombre de penetrations") {
    const auto railA = open_path({{0, 0}, {10'000, 0}});
    const auto railB = open_path({{0, 2'000}, {10'000, 2'000}});
    SatinConfig dense;
    dense.density = Micrometers{500};
    SatinConfig coarse;
    coarse.density = Micrometers{2'000};
    CHECK(fill_satin(railA, railB, dense).satin.size() >
          fill_satin(railA, railB, coarse).satin.size());
}

TEST_CASE("satin : compensation de tirage elargit la colonne") {
    const auto railA = open_path({{0, 0}, {10'000, 0}});
    const auto railB = open_path({{0, 4'000}, {10'000, 4'000}});
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    cfg.pull_compensation = Micrometers{500};
    const auto result = fill_satin(railA, railB, cfg);
    // Les points debordent de 0,5 mm de chaque cote (y=-500 et y=4500).
    bool below = false;
    bool above = false;
    for (const Vec2um& p : result.satin) {
        if (p.y.value <= -500) below = true;
        if (p.y.value >= 4'500) above = true;
    }
    CHECK(below);
    CHECK(above);
}

TEST_CASE("satin : sous-couche centrale sur l'axe") {
    const auto railA = open_path({{0, 0}, {12'000, 0}});
    const auto railB = open_path({{0, 4'000}, {12'000, 4'000}});
    SatinConfig cfg;
    cfg.center_underlay = true;
    cfg.underlay_spacing = Micrometers{3'000};
    const auto result = fill_satin(railA, railB, cfg);
    REQUIRE_FALSE(result.underlay.empty());
    // Axe central : y = 2000.
    for (const Vec2um& p : result.underlay) {
        CHECK(p.y.value == 2'000);
    }
}

TEST_CASE("satin : rails degeneres -> resultat vide") {
    const auto single = open_path({{0, 0}});
    CHECK(fill_satin(single, single, {}).satin.empty());
}

TEST_CASE("satin : deterministe") {
    const auto railA = open_path({{0, 0}, {15'000, 1'000}});
    const auto railB = open_path({{0, 5'000}, {15'000, 6'000}});
    SatinConfig cfg;
    cfg.density = Micrometers{600};
    CHECK(fill_satin(railA, railB, cfg).satin == fill_satin(railA, railB, cfg).satin);
}

TEST_CASE("rails_from_contour : rectangle allonge -> deux longs rails") {
    // Rectangle 20x4 mm : les bouts sont les cotes courts.
    geometry::Path rect;
    rect.closed = true;
    rect.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{20'000}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{20'000}, Micrometers{4'000}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{0}, Micrometers{4'000}}, geometry::NodeType::Corner, {}, {}},
    };
    const auto rails = rails_from_contour(rect);
    REQUIRE(rails.has_value());
    // Les deux rails doivent produire un satin coherent (largeur ~4 mm).
    SatinConfig cfg;
    cfg.density = Micrometers{1'000};
    const auto result = fill_satin(rails->first, rails->second, cfg);
    REQUIRE_FALSE(result.satin.empty());
    CHECK(result.max_width_um > 3'000.0);
    CHECK(result.max_width_um < 6'000.0);
}

TEST_CASE("rails_from_contour : contour trop petit -> nullopt") {
    geometry::Path tri;
    tri.closed = true;
    tri.nodes = {{Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}}};
    CHECK_FALSE(rails_from_contour(tri).has_value());
}
