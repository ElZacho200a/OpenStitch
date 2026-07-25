// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "openstitch/stitch_generation/tatami.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

geometry::Path rect(std::int32_t w, std::int32_t h) {
    geometry::Path p;
    p.closed = true;
    p.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{w}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{w}, Micrometers{h}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{0}, Micrometers{h}}, geometry::NodeType::Corner, {}, {}},
    };
    return p;
}

document::TatamiParams params(std::int32_t spacing, std::int32_t len, double angle = 0.0) {
    document::TatamiParams p;
    p.row_spacing = Micrometers{spacing};
    p.stitch_length = Micrometers{len};
    p.angle = Angle{angle};
    p.inset = Micrometers{0};
    return p;
}

bool inside_rect(Vec2um p, std::int32_t w, std::int32_t h, std::int32_t tol = 2) {
    return p.x.value >= -tol && p.x.value <= w + tol && p.y.value >= -tol && p.y.value <= h + tol;
}

// Positions seules (indépendamment du drapeau couture/déplacement).
std::vector<Vec2um> positions(const std::vector<FillStitch>& f) {
    std::vector<Vec2um> out;
    out.reserve(f.size());
    for (const auto& fs : f) {
        out.push_back(fs.pos);
    }
    return out;
}

}  // namespace

TEST_CASE("tatami : rectangle rempli, points dans la forme") {
    // 20x10 mm, rangées tous les 1 mm -> ~10 rangees.
    const auto pts = positions(fill_tatami({rect(20'000, 10'000), {}}, params(1'000, 4'000)));
    REQUIRE(pts.size() > 10);
    for (const Vec2um& p : pts) {
        CHECK(inside_rect(p, 20'000, 10'000));
    }
}

TEST_CASE("tatami : le nombre de rangees suit la densite") {
    // Compte les valeurs de y distinctes (angle 0 -> rangees horizontales).
    const auto pts = positions(fill_tatami({rect(20'000, 10'000), {}}, params(1'000, 4'000)));
    std::set<std::int32_t> rows;
    for (const Vec2um& p : pts) {
        rows.insert(p.y.value);
    }
    // Hauteur 10 mm, pas 1 mm : environ 10 rangees (+/- 1).
    CHECK(rows.size() >= 9);
    CHECK(rows.size() <= 11);
}

TEST_CASE("tatami : serpentin (rangees en sens alterne)") {
    const auto pts = positions(fill_tatami({rect(20'000, 4'000), {}}, params(1'000, 4'000)));
    REQUIRE(pts.size() >= 4);
    // Le remplissage doit balayer toute la largeur : min et max x atteints.
    std::int32_t minX = INT32_MAX, maxX = INT32_MIN;
    for (const Vec2um& p : pts) {
        minX = std::min(minX, p.x.value);
        maxX = std::max(maxX, p.x.value);
    }
    CHECK(minX <= 100);
    CHECK(maxX >= 19'900);
}

namespace {
geometry::PathSet ring_shape() {
    // Anneau : exterieur 20x20, trou central 8x8 (de 6,6 a 14,14 mm).
    geometry::PathSet ring;
    ring.outer = rect(20'000, 20'000);
    geometry::Path hole;
    hole.closed = true;
    hole.nodes = {
        {Vec2um{Micrometers{6'000}, Micrometers{6'000}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{14'000}, Micrometers{6'000}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{14'000}, Micrometers{14'000}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{6'000}, Micrometers{14'000}}, geometry::NodeType::Corner, {}, {}},
    };
    ring.holes.push_back(hole);
    return ring;
}
}  // namespace

TEST_CASE("tatami : trou respecte (pas de points dans le trou)") {
    const auto pts = positions(fill_tatami(ring_shape(), params(1'000, 4'000)));
    REQUIRE_FALSE(pts.empty());
    for (const Vec2um& p : pts) {
        const bool strictlyInHole = p.x.value > 6'500 && p.x.value < 13'500 &&
                                    p.y.value > 6'500 && p.y.value < 13'500;
        CHECK_FALSE(strictlyInHole);
    }
}

TEST_CASE("tatami : AUCUN point cousu ne traverse le trou (routage)") {
    // Correctif du debordement : un segment cousu (deux points consecutifs dont
    // le second n'est PAS un deplacement) ne doit jamais passer par le trou.
    const auto fill = fill_tatami(ring_shape(), params(1'000, 4'000));
    REQUIRE(fill.size() >= 2);
    int crossings = 0;
    for (std::size_t i = 1; i < fill.size(); ++i) {
        if (fill[i].travel) {
            continue;  // deplacement : autorise a traverser
        }
        const Vec2um a = fill[i - 1].pos;
        const Vec2um b = fill[i].pos;
        const Vec2um mid{Micrometers{(a.x.value + b.x.value) / 2},
                         Micrometers{(a.y.value + b.y.value) / 2}};
        const bool midInHole = mid.x.value > 6'200 && mid.x.value < 13'800 &&
                               mid.y.value > 6'200 && mid.y.value < 13'800;
        if (midInHole) {
            ++crossings;
        }
    }
    CHECK(crossings == 0);
    // Et il existe bien au moins un deplacement (contournement du trou).
    CHECK(std::any_of(fill.begin(), fill.end(), [](const FillStitch& f) { return f.travel; }));
}

TEST_CASE("tatami : longueur de point respectee le long des rangees") {
    const auto pts = positions(fill_tatami({rect(30'000, 3'000), {}}, params(1'000, 3'000)));
    REQUIRE(pts.size() >= 2);
    // Sur une meme rangee (meme y), l'ecart entre points consecutifs <= 3 mm.
    for (std::size_t i = 1; i < pts.size(); ++i) {
        if (pts[i].y == pts[i - 1].y) {
            CHECK(length_um(pts[i] - pts[i - 1]) <= 3'050.0);
        }
    }
}

TEST_CASE("tatami : angle 90 degres remplit aussi la forme") {
    const auto pts = positions(
        fill_tatami({rect(10'000, 20'000), {}}, params(1'000, 4'000, std::acos(-1.0) / 2.0)));
    REQUIRE(pts.size() > 10);
    for (const Vec2um& p : pts) {
        CHECK(inside_rect(p, 10'000, 20'000, 50));
    }
}

TEST_CASE("tatami : forme degeneree -> vide sans crash") {
    geometry::Path tiny;
    tiny.closed = true;
    tiny.nodes = {{Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}}};
    CHECK(fill_tatami({tiny, {}}, params(1'000, 4'000)).empty());
}

TEST_CASE("tatami : deterministe") {
    const auto a = fill_tatami({rect(20'000, 10'000), {}}, params(700, 3'000));
    const auto b = fill_tatami({rect(20'000, 10'000), {}}, params(700, 3'000));
    CHECK(a == b);
}
