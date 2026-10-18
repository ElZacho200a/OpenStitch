// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/shapes.hpp"

using namespace openstitch;
using namespace openstitch::geometry;
using Catch::Approx;

namespace {

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

struct Box {
    double minx{1e18}, miny{1e18}, maxx{-1e18}, maxy{-1e18};
};

Box bbox(const Path& path) {
    Box b;
    for (const Vec2um& v : flatten(path, Micrometers{5}).points) {
        b.minx = std::min(b.minx, static_cast<double>(v.x.value));
        b.maxx = std::max(b.maxx, static_cast<double>(v.x.value));
        b.miny = std::min(b.miny, static_cast<double>(v.y.value));
        b.maxy = std::max(b.maxy, static_cast<double>(v.y.value));
    }
    return b;
}

double flat_area(const Path& path) {
    return signed_area_um2(flatten_to_polygon(path, Micrometers{5}));
}

} // namespace

TEST_CASE("star_path : 2n coins alternant rayons, antihoraire") {
    const Path s = star_path(pt(0, 0), Micrometers{10'000}, Micrometers{4'000}, 5);
    REQUIRE(s.nodes.size() == 10);
    CHECK(s.closed);
    for (std::size_t i = 0; i < s.nodes.size(); ++i) {
        const double r = length_um(s.nodes[i].pos);
        CHECK(r == Approx(i % 2 == 0 ? 10'000.0 : 4'000.0).margin(1.0));
        CHECK(s.nodes[i].type == NodeType::Corner);
    }
    // Premiere pointe au nord par defaut.
    CHECK(s.nodes[0].pos.x.value == 0);
    CHECK(s.nodes[0].pos.y.value == 10'000);
    CHECK(signed_area_um2(s) > 0.0);
}

TEST_CASE("star_path : entrees degenerees donnent un chemin vide") {
    CHECK(star_path(pt(0, 0), Micrometers{10'000}, Micrometers{4'000}, 2).nodes.empty());
    CHECK(star_path(pt(0, 0), Micrometers{10'000}, Micrometers{10'000}, 5).nodes.empty());
    CHECK(star_path(pt(0, 0), Micrometers{10'000}, Micrometers{0}, 5).nodes.empty());
    CHECK(star_path(pt(0, 0), Micrometers{0}, Micrometers{0}, 5).nodes.empty());
}

TEST_CASE("heart_path : inscrit dans son cadre, symetrique, antihoraire") {
    const Path h = heart_path(pt(10'000, 0), pt(-10'000, 18'000)); // coins dans le desordre
    REQUIRE_FALSE(h.nodes.empty());
    const Box b = bbox(h);
    CHECK(b.minx == Approx(-10'000.0).margin(30.0));
    CHECK(b.maxx == Approx(10'000.0).margin(30.0));
    CHECK(b.miny == Approx(0.0).margin(30.0));
    CHECK(b.maxy == Approx(18'000.0).margin(30.0));
    CHECK(flat_area(h) > 0.0);
    // Symetrie gauche/droite des noeuds : le miroir de chaque noeud existe.
    for (const auto& n : h.nodes) {
        const bool found = std::any_of(h.nodes.begin(), h.nodes.end(), [&](const PathNode& m) {
            return m.pos.x.value == -n.pos.x.value && m.pos.y == n.pos.y;
        });
        CHECK(found);
    }
    CHECK(heart_path(pt(0, 0), pt(0, 5'000)).nodes.empty());
}

TEST_CASE("arrow_path : fleche horizontale, 7 coins, dimensions exactes") {
    const Path a = arrow_path(pt(0, 0), pt(30'000, 0), Micrometers{4'000}, Micrometers{10'000},
                              Micrometers{8'000});
    REQUIRE(a.nodes.size() == 7);
    CHECK(a.nodes[3].pos == pt(30'000, 0));      // pointe
    CHECK(a.nodes[1].pos == pt(22'000, -2'000)); // base de tete, corps
    CHECK(a.nodes[2].pos == pt(22'000, -5'000)); // aile
    CHECK(a.nodes[0].pos == pt(0, -2'000));
    CHECK(signed_area_um2(a) > 0.0);
    // Aire : corps 22 x 4 mm + tete triangle 8 x 10 / 2 mm.
    CHECK(signed_area_um2(a) == Approx(22'000.0 * 4'000.0 + 0.5 * 8'000.0 * 10'000.0));
}

TEST_CASE("arrow_path : tete bornee, orientation quelconque, degenere vide") {
    const Path a = arrow_path(pt(0, 0), pt(0, 10'000), Micrometers{2'000}, Micrometers{6'000},
                              Micrometers{50'000});
    REQUIRE(a.nodes.size() == 7);
    CHECK(a.nodes[3].pos == pt(0, 10'000));
    CHECK(a.nodes[1].pos.y.value == 0); // tete = toute la longueur
    CHECK(arrow_path(pt(5, 5), pt(5, 5), Micrometers{10}, Micrometers{20}, Micrometers{5})
              .nodes.empty());
    CHECK(arrow_path(pt(0, 0), pt(100, 0), Micrometers{0}, Micrometers{20}, Micrometers{5})
              .nodes.empty());
}

TEST_CASE("rounded_rectangle_path : cadre exact, aire inferieure au rectangle") {
    const Path r = rounded_rectangle_path(pt(0, 0), pt(40'000, 20'000), Micrometers{5'000});
    REQUIRE(r.nodes.size() == 8);
    const Box b = bbox(r);
    CHECK(b.minx == Approx(0.0).margin(1.0));
    CHECK(b.maxx == Approx(40'000.0).margin(1.0));
    CHECK(b.miny == Approx(0.0).margin(1.0));
    CHECK(b.maxy == Approx(20'000.0).margin(1.0));
    // Aire exacte : w*h - (4 - pi) r^2.
    const double expected = 40'000.0 * 20'000.0 - (4.0 - std::numbers::pi) * 5'000.0 * 5'000.0;
    CHECK(flat_area(r) == Approx(expected).epsilon(0.001));
}

TEST_CASE("rounded_rectangle_path : rayon borne et nul") {
    const Path sharp = rounded_rectangle_path(pt(0, 0), pt(10'000, 6'000), Micrometers{0});
    CHECK(sharp.nodes.size() == 4);
    const Path big = rounded_rectangle_path(pt(0, 0), pt(10'000, 6'000), Micrometers{999'999});
    const Box b = bbox(big);
    CHECK(b.maxx == Approx(10'000.0).margin(1.0));
    CHECK(b.maxy == Approx(6'000.0).margin(1.0));
    // Rayon borne a la moitie du petit cote : stade (aire < rectangle).
    CHECK(flat_area(big) < 10'000.0 * 6'000.0);
    CHECK(rounded_rectangle_path(pt(0, 0), pt(0, 6'000), Micrometers{100}).nodes.empty());
}

TEST_CASE("arc_path : demi-cercle ouvert, extremites et sens de balayage") {
    const Path arc = arc_path(pt(0, 0), Micrometers{10'000}, 0.0, std::numbers::pi);
    CHECK_FALSE(arc.closed);
    REQUIRE(arc.nodes.size() == 3); // deux quarts de tour
    CHECK(arc.nodes.front().pos == pt(10'000, 0));
    CHECK(arc.nodes.back().pos == pt(-10'000, 0));
    const Polyline f = flatten(arc, Micrometers{2});
    for (const Vec2um& v : f.points) {
        CHECK(length_um(v) == Approx(10'000.0).margin(15.0));
        CHECK(v.y.value >= -1);
    }
    // Balayage negatif : passe par le sud.
    const Path cw = arc_path(pt(0, 0), Micrometers{10'000}, 0.0, -std::numbers::pi);
    CHECK(cw.nodes.back().pos == pt(-10'000, 0));
    for (const Vec2um& v : flatten(cw, Micrometers{2}).points) {
        CHECK(v.y.value <= 1);
        CHECK(length_um(v) == Approx(10'000.0).margin(15.0));
    }
}

TEST_CASE("arc_path : cas degeneres") {
    CHECK(arc_path(pt(0, 0), Micrometers{0}, 0.0, 1.0).nodes.empty());
    CHECK(arc_path(pt(0, 0), Micrometers{100}, 0.0, 0.0).nodes.empty());
    // Plus de 2 pi est borne : tour complet = 4 segments.
    CHECK(arc_path(pt(0, 0), Micrometers{100}, 0.0, 20.0).nodes.size() == 5);
}

TEST_CASE("spiral_path : rayon croissant, extremites, deterministe") {
    const Path s = spiral_path(pt(0, 0), Micrometers{1'000}, Micrometers{13'000}, 3.0);
    CHECK_FALSE(s.closed);
    REQUIRE(s.nodes.size() == 37); // 3 tours x 12 + 1
    CHECK(length_um(s.nodes.front().pos) == Approx(1'000.0).margin(1.0));
    CHECK(length_um(s.nodes.back().pos) == Approx(13'000.0).margin(1.0));
    for (std::size_t i = 1; i < s.nodes.size(); ++i) {
        CHECK(length_um(s.nodes[i].pos) > length_um(s.nodes[i - 1].pos));
    }
    CHECK(s == spiral_path(pt(0, 0), Micrometers{1'000}, Micrometers{13'000}, 3.0));
    CHECK(spiral_path(pt(0, 0), Micrometers{1'000}, Micrometers{13'000}, 0.0).nodes.empty());
}
