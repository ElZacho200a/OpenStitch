// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/geometry/offset.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

PathSet square(std::int32_t s) {
    Path p;
    p.closed = true;
    p.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{s}, Micrometers{0}}, NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{s}, Micrometers{s}}, NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{0}, Micrometers{s}}, NodeType::Corner, {}, {}},
    };
    return {p, {}};
}

// Carre `outerSide` troue d'un carre [holeMin,holeMax], le trou construit
// avec LA MEME orientation que l'exterieur (`square()`) -- reproduit le cas
// reel trouve sur la fixture `auto_satin::shapes::make_shape("ring")`, dont
// le trou et l'exterieur sont tous deux generes par le meme parcours d'angle
// croissant (audit satin_coverage, 2026-08-13).
PathSet annulus_same_orientation_hole(std::int32_t outerSide, std::int32_t holeMin, std::int32_t holeMax) {
    PathSet ps = square(outerSide);
    Path hole;
    hole.closed = true;
    hole.nodes = {
        {Vec2um{Micrometers{holeMin}, Micrometers{holeMin}}, NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{holeMax}, Micrometers{holeMin}}, NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{holeMax}, Micrometers{holeMax}}, NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{holeMin}, Micrometers{holeMax}}, NodeType::Corner, {}, {}},
    };
    ps.holes.push_back(hole);
    return ps;
}

}  // namespace

TEST_CASE("inset : retrait interieur reduit l'aire") {
    // Carre 10x10 mm, retrait 1 mm -> carre 8x8 mm = 64 mm².
    const auto result = inset_path_set(square(10'000), Micrometers{1'000});
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    const double area = std::abs(signed_area_um2((*result)[0].outer)) / 1e6;
    CHECK(area > 60.0);
    CHECK(area < 68.0);
}

TEST_CASE("inset : retrait nul rend la forme inchangee") {
    const auto result = inset_path_set(square(10'000), Micrometers{0});
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(std::abs(signed_area_um2((*result)[0].outer)) == 100'000'000.0);
}

TEST_CASE("inset : retrait trop grand fait disparaitre la forme") {
    const auto result = inset_path_set(square(10'000), Micrometers{6'000});
    REQUIRE(result.has_value());
    CHECK(result->empty());
}

TEST_CASE("inset : trou de meme orientation que l'exterieur -- erosion correcte quand meme") {
    // Anneau 20x20mm troue d'un carre 10x10mm centre (aire 300 mm2). `delta`
    // > 0 doit a la fois RETRECIR l'exterieur ET GRANDIR le trou (moins de
    // matiere de part et d'autre) -- defaut trouve (satin_coverage,
    // 2026-08-13) : sans reorientation defensive avant Clipper2, un trou de
    // MEME orientation que l'exterieur pouvait retrecir au lieu de grandir,
    // faussant toute erosion d'une region a trou construite comme ceci.
    const auto target = annulus_same_orientation_hole(20'000, 5'000, 15'000);
    const auto result = inset_path_set(target, Micrometers{1'000});
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    REQUIRE(result->front().holes.size() == 1);
    double area = std::abs(signed_area_um2(result->front().outer));
    for (const auto& hole : result->front().holes) {
        area -= std::abs(signed_area_um2(hole));
    }
    area /= 1e6;
    // Attendu : exterieur 18x18 - trou 12x12 = 180 mm2 (le trou grandit sous
    // erosion -- s'il avait retreci, l'aire nette serait restee proche des
    // 300 mm2 d'origine au lieu de chuter a 180).
    CHECK(area > 170.0);
    CHECK(area < 190.0);
}
