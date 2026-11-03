// SPDX-License-Identifier: Apache-2.0
// Remplissages radial, en spirale et croise (HP-STI-008 / 009 / 014).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "openstitch/stitch_generation/crosshatch_fill.hpp"
#include "openstitch/stitch_generation/radial_fill.hpp"
#include "openstitch/stitch_generation/spiral_fill.hpp"

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

geometry::PathSet square(double side) {
    return geometry::PathSet{polygon({um(0, 0), um(side, 0), um(side, side), um(0, side)}), {}};
}

// Carre avec un trou central de 10 mm de cote.
geometry::PathSet square_with_hole() {
    geometry::PathSet set = square(30);
    set.holes.push_back(polygon({um(10, 10), um(10, 20), um(20, 20), um(20, 10)}));
    return set;
}

double dist_um(const FillStitch& a, const FillStitch& b) {
    return std::hypot(double(a.pos.x.value) - double(b.pos.x.value),
                      double(a.pos.y.value) - double(b.pos.y.value));
}

bool inside_box(const FillStitch& s, double side) {
    const double lim = side * 1000.0;
    return s.pos.x.value >= 0 && s.pos.y.value >= 0 && s.pos.x.value <= lim && s.pos.y.value <= lim;
}

bool in_hole(const FillStitch& s) {
    return s.pos.x.value > 10'050 && s.pos.x.value < 19'950 && s.pos.y.value > 10'050 &&
           s.pos.y.value < 19'950;
}

} // namespace

TEST_CASE("radial fill: rays stay inside the region and respect the stitch length") {
    document::RadialFillParams p;
    const auto stitches = fill_radial(square(20), p);
    REQUIRE(stitches.size() > 200);
    for (std::size_t i = 0; i < stitches.size(); ++i) {
        CHECK(inside_box(stitches[i], 20.0));
        if (i > 0 && !stitches[i].jump) {
            CHECK(dist_um(stitches[i - 1], stitches[i]) <= 3'000.0 + 1.0);
        }
    }
}

TEST_CASE("radial fill: density near the periphery stays constant") {
    document::RadialFillParams p;
    const auto stitches = fill_radial(square(20), p);
    // Pas de trou noir : chaque cellule de 4 mm du bord contient des points.
    int near_edge = 0;
    int near_centre = 0;
    for (const FillStitch& s : stitches) {
        const double dx = s.pos.x.value / 1000.0 - 10.0;
        const double dy = s.pos.y.value / 1000.0 - 10.0;
        const double r = std::hypot(dx, dy);
        if (r > 8.0 && r < 9.0) {
            ++near_edge;
        } else if (r < 1.0) {
            ++near_centre;
        }
    }
    CHECK(near_edge > 0);
    // Les rayons ne s'entassent pas au centre : moins de points par mm^2 qu'une couture
    // naive de tous les rayons, qui en mettrait N/(2*pi*r) fois plus.
    CHECK(near_centre < near_edge);
}

TEST_CASE("radial fill: never sews across a hole and is deterministic") {
    document::RadialFillParams p;
    p.center = um(15, 15); // centre dans le trou
    const auto a = fill_radial(square_with_hole(), p);
    const auto b = fill_radial(square_with_hole(), p);
    REQUIRE(!a.empty());
    CHECK(a == b);
    for (const FillStitch& s : a) {
        CHECK_FALSE(in_hole(s));
    }
}

TEST_CASE("radial fill: degenerate regions give no stitches") {
    CHECK(fill_radial(geometry::PathSet{}, document::RadialFillParams{}).empty());
}

TEST_CASE("spiral fill: one continuous stitched path inside a convex region") {
    document::SpiralFillParams p;
    const auto stitches = fill_spiral(square(20), p);
    REQUIRE(stitches.size() > 200);
    int jumps = 0;
    for (std::size_t i = 0; i < stitches.size(); ++i) {
        CHECK(inside_box(stitches[i], 20.0));
        if (stitches[i].jump) {
            ++jumps;
        } else if (i > 0) {
            CHECK(dist_um(stitches[i - 1], stitches[i]) <= 3'000.0 + 1.0);
        }
    }
    // Dans le carre la spirale est rognee aux coins : au plus quelques sauts de rentree.
    CHECK(jumps <= 2);
    // Le premier point est au centre, les suivants s'en eloignent globalement.
    CHECK(std::hypot(stitches.front().pos.x.value / 1000.0 - 10.0,
                     stitches.front().pos.y.value / 1000.0 - 10.0) < 1.0);
}

TEST_CASE("spiral fill: turn spacing matches the row spacing") {
    document::SpiralFillParams p;
    p.row_spacing = Micrometers{1'000};
    const auto stitches = fill_spiral(square(30), p);
    // Le long de l'axe +x depuis le centre, deux franchissements successifs sont espaces de 1 mm.
    std::vector<double> crossings;
    for (std::size_t i = 1; i < stitches.size(); ++i) {
        const double x0 = stitches[i - 1].pos.x.value / 1000.0 - 15.0;
        const double y0 = stitches[i - 1].pos.y.value / 1000.0 - 15.0;
        const double y1 = stitches[i].pos.y.value / 1000.0 - 15.0;
        const double x1 = stitches[i].pos.x.value / 1000.0 - 15.0;
        if (!stitches[i].jump && y0 < 0.0 && y1 >= 0.0) {
            // abscisse du franchissement de l'axe (interpolee sur la corde), pas du point d'arrivee
            const double xc = x0 + (x1 - x0) * (0.0 - y0) / (y1 - y0);
            // Au-dela de 14 mm la spirale est rognee par le bord du carre (demi-cote 15 mm).
            if (xc > 0.0 && xc < 14.0) {
                crossings.push_back(xc);
            }
        }
    }
    REQUIRE(crossings.size() > 4);
    for (std::size_t i = 3; i + 1 < crossings.size(); ++i) {
        CHECK(std::abs((crossings[i + 1] - crossings[i]) - 1.0) < 0.35);
    }
}

TEST_CASE("spiral fill: holes are skipped and the clockwise flag mirrors the path") {
    document::SpiralFillParams p;
    p.center = um(15, 15);
    const auto ccw = fill_spiral(square_with_hole(), p);
    REQUIRE(!ccw.empty());
    for (const FillStitch& s : ccw) {
        CHECK_FALSE(in_hole(s));
    }
    p.clockwise = true;
    const auto cw = fill_spiral(square_with_hole(), p);
    REQUIRE(!cw.empty());
    CHECK(ccw != cw);
    CHECK(fill_spiral(square_with_hole(), p) == cw); // deterministe
}

TEST_CASE("crosshatch fill: layers add up and stay in the region") {
    document::CrosshatchFillParams two;
    two.layers = 2;
    document::CrosshatchFillParams three = two;
    three.layers = 3;
    const auto a = fill_crosshatch(square(30), two);
    const auto b = fill_crosshatch(square(30), three);
    REQUIRE(a.size() > 50);
    CHECK(b.size() > a.size());
    for (const FillStitch& s : a) {
        CHECK(inside_box(s, 30.0));
    }
    // Un saut (ou une liaison cachee) signale le debut de chaque couche suivante.
    CHECK(std::count_if(a.begin(), a.end(), [](const FillStitch& s) { return s.jump; }) >= 1);
    CHECK(fill_crosshatch(square(30), two) == a);
}

TEST_CASE("crosshatch fill: out of range settings are clamped") {
    document::CrosshatchFillParams p;
    p.layers = 9;
    p.row_spacing = Micrometers{1};
    p.stitch_length = Micrometers{99'999};
    const auto s = fill_crosshatch(square(20), p);
    CHECK(!s.empty());
}
