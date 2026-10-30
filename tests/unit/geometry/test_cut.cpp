// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/cut.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

Vec2um p(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

PathNode corner(Vec2um pos) {
    return PathNode{pos, NodeType::Corner, std::nullopt, std::nullopt};
}

PathSet square(std::int32_t s) {
    Path path;
    path.closed = true;
    path.nodes = {corner(p(0, 0)), corner(p(s, 0)), corner(p(s, s)), corner(p(0, s))};
    return {path, {}};
}

// Forme "haltère" : deux carrés (10x10 mm) reliés par un pont fin (2 mm de
// large) -- simule une jonction Y/T à séparer via une ligne de coupe, le cas
// d'usage principal (§ audit satin, comparaison Ink/Stitch "cut lines").
PathSet dumbbell() {
    Path path;
    path.closed = true;
    path.nodes = {
        corner(p(0, 0)),           corner(p(10'000, 0)),      corner(p(10'000, 4'000)),
        corner(p(20'000, 4'000)),  corner(p(20'000, 0)),      corner(p(30'000, 0)),
        corner(p(30'000, 10'000)), corner(p(20'000, 10'000)), corner(p(20'000, 6'000)),
        corner(p(10'000, 6'000)),  corner(p(10'000, 10'000)), corner(p(0, 10'000)),
    };
    return {path, {}};
}

} // namespace

TEST_CASE("cut_path_set : ligne verticale separe un carre en deux") {
    // Carre 10x10 mm, coupe verticale a mi-largeur, largement prolongee.
    const auto result = cut_path_set(square(10'000), p(5'000, -5'000), p(5'000, 15'000));
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    double totalArea = 0.0;
    for (const auto& piece : *result) {
        const double area = std::abs(signed_area_um2(piece.outer)) / 1e6;
        CHECK(area > 45.0); // ~50 mm2 chacun, bande de coupe negligeable
        CHECK(area < 50.0);
        totalArea += area;
    }
    CHECK(totalArea > 95.0); // aire totale ~inchangee (bande de coupe fine)
}

TEST_CASE("cut_path_set : coupe le pont d'un halte -> deux carres separes") {
    const auto result = cut_path_set(dumbbell(), p(15'000, -5'000), p(15'000, 15'000));
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    for (const auto& piece : *result) {
        const double area = std::abs(signed_area_um2(piece.outer)) / 1e6;
        // Carre (100 mm2) + moitie du pont (5 x 2 mm = 10 mm2) chacun.
        CHECK(area > 105.0);
        CHECK(area < 115.0);
    }
}

TEST_CASE("cut_path_set : ligne hors de la region -> un seul morceau inchange") {
    const auto result = cut_path_set(square(10'000), p(50'000, 0), p(50'000, 10'000));
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    const double area = std::abs(signed_area_um2((*result)[0].outer)) / 1e6;
    CHECK(area > 99.0);
    CHECK(area < 100.01);
}

TEST_CASE("cut_path_set : points confondus -> aucune coupe") {
    const auto result = cut_path_set(square(10'000), p(5'000, 5'000), p(5'000, 5'000));
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(std::abs(signed_area_um2((*result)[0].outer)) / 1e6 == 100.0);
}

TEST_CASE("cut_path_set : deterministe") {
    const auto a = cut_path_set(dumbbell(), p(15'000, -5'000), p(15'000, 15'000));
    const auto b = cut_path_set(dumbbell(), p(15'000, -5'000), p(15'000, 15'000));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->size() == b->size());
    for (std::size_t i = 0; i < a->size(); ++i) {
        CHECK((*a)[i].outer.nodes == (*b)[i].outer.nodes);
    }
}

TEST_CASE("cut_path_set : la bande retiree reste fine (aire totale quasi inchangee)") {
    // Meme coupe qu'au premier test, mais avec une bande plus large (0,5 mm) :
    // verifie que l'aire perdue reste bornee par la largeur demandee, pas
    // beaucoup plus (pas de sur-decoupe).
    const auto result =
        cut_path_set(square(10'000), p(5'000, -5'000), p(5'000, 15'000), Micrometers{500});
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    double totalArea = 0.0;
    for (const auto& piece : *result) {
        totalArea += std::abs(signed_area_um2(piece.outer)) / 1e6;
    }
    // Aire perdue = bande (0,5 mm) x hauteur (10 mm) = 5 mm2 -> reste ~95 mm2.
    CHECK(totalArea > 93.0);
    CHECK(totalArea < 97.0);
}

// Régression : une forme en anneau possède deux ponts autour d'un vide.
// Couper celui du haut laisse l'ensemble connecté par le bas.
TEST_CASE("cut_path_set_bounded: bridge cut keeps one connected shape", "[cut][regression]") {
    PathSet ring = square(30'000);
    Path hole;
    hole.closed = true;
    hole.nodes.push_back(corner(p(10'000, 10'000)));
    hole.nodes.push_back(corner(p(20'000, 10'000)));
    hole.nodes.push_back(corner(p(20'000, 20'000)));
    hole.nodes.push_back(corner(p(10'000, 20'000)));
    ring.holes.push_back(hole);

    const auto start = p(15'000, -5'000);
    const auto end = p(15'000, 15'000);
    const auto cut = cut_path_set_bounded(ring, start, end, 10'000.0, Micrometers{20});
    REQUIRE(cut.has_value());
    // L'autre pont maintient la connexion et le vide s'ouvre vers l'extérieur.
    REQUIRE(cut->size() == 1);
    CHECK(cut->front().holes.empty());
    CHECK(path_set_area_um2(cut->front()) < path_set_area_um2(ring));
    CHECK(std::abs(path_set_area_um2(cut->front()) / 1e6 - 799.8) < 0.01);
}

TEST_CASE("cut_path_set_bounded: miss preserves original curved geometry", "[cut][regression]") {
    PathSet shape = square(10'000);
    // Une tangente doit survivre intacte si la coupe n'atteint pas la forme.
    shape.outer.nodes[0].type = NodeType::Smooth;
    shape.outer.nodes[0].tan_out = p(500, 0);
    const auto start = p(20'000, -5'000);
    const auto end = p(20'000, 15'000);
    const auto cut = cut_path_set_bounded(shape, start, end, 10'000.0, Micrometers{20});
    REQUIRE(cut.has_value());
    REQUIRE(cut->size() == 1);
    CHECK(cut->front() == shape);
}
