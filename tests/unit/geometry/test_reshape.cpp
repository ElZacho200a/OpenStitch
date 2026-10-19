// SPDX-License-Identifier: Apache-2.0
// Remodelage d'un segment (HP-VEC-016).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/reshape.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

PathNode corner(std::int32_t x, std::int32_t y) {
    return PathNode{pt(x, y), NodeType::Corner, std::nullopt, std::nullopt};
}

Path square(std::int32_t s) {
    Path p;
    p.closed = true;
    p.nodes = {corner(0, 0), corner(s, 0), corner(s, s), corner(0, s)};
    return p;
}

// Courbe ouverte lisse : A(0,0) -> B(10000,0) -> C(20000,0), nœud B lisse avec tangentes.
Path smooth_chain() {
    Path p;
    p.closed = false;
    p.nodes.push_back(PathNode{pt(0, 0), NodeType::Corner, std::nullopt, pt(3000, 0)});
    p.nodes.push_back(PathNode{pt(10000, 0), NodeType::Smooth, pt(-3000, 0), pt(3000, 0)});
    p.nodes.push_back(PathNode{pt(20000, 0), NodeType::Corner, pt(-3000, 0), std::nullopt});
    return p;
}

} // namespace

TEST_CASE("nearest_segment finds the closest edge and parameter", "[geometry][reshape]") {
    const Path sq = square(10'000);
    const auto hit = nearest_segment(sq, pt(5'000, -300), 1'000.0);
    REQUIRE(hit.has_value());
    CHECK(hit->segment == 0);
    CHECK_THAT(hit->t, Catch::Matchers::WithinAbs(0.5, 1e-3));
    CHECK_THAT(hit->distance_um, Catch::Matchers::WithinAbs(300.0, 1.0));
    // Le segment de fermeture [3 -> 0] est atteignable sur un chemin fermé.
    const auto closing = nearest_segment(sq, pt(-200, 5'000), 1'000.0);
    REQUIRE(closing.has_value());
    CHECK(closing->segment == 3);
    // Trop loin : aucun résultat.
    CHECK_FALSE(nearest_segment(sq, pt(5'000, -5'000), 1'000.0).has_value());
    // Chemin ouvert : pas de segment de fermeture.
    Path open = sq;
    open.closed = false;
    CHECK_FALSE(nearest_segment(open, pt(-200, 5'000), 1'000.0).has_value());
}

TEST_CASE("nearest_segment works on a cubic segment", "[geometry][reshape]") {
    const Path chain = smooth_chain();
    const auto hit = nearest_segment(chain, pt(9'500, 40), 500.0);
    REQUIRE(hit.has_value());
    CHECK(hit->segment == 0);
    CHECK(hit->t > 0.9);
}

TEST_CASE("reshape_segment keeps node count and end nodes, curves a straight edge",
          "[geometry][reshape]") {
    const Path sq = square(10'000);
    const Path out = reshape_segment(sq, 0, 0.5, pt(0, -2'000));
    REQUIRE(out.nodes.size() == sq.nodes.size());
    for (std::size_t i = 0; i < sq.nodes.size(); ++i) {
        CHECK(out.nodes[i].pos == sq.nodes[i].pos); // extrémités intactes
    }
    // Les angles marqués du carré restent des coins ; les autres segments restent droits.
    CHECK(out.nodes[0].type == NodeType::Corner);
    CHECK_FALSE(out.nodes[0].tan_in.has_value());
    CHECK_FALSE(out.nodes[1].tan_out.has_value());
    // Le point milieu de la courbe a suivi le curseur.
    Path single;
    single.closed = false;
    single.nodes = {out.nodes[0], out.nodes[1]};
    const Polyline flat = flatten(single, Micrometers{5});
    std::int32_t minY = 0;
    for (const auto& p : flat.points) {
        minY = std::min(minY, p.y.value);
    }
    CHECK(minY < -1'400);
}

TEST_CASE("reshape_segment follows the cursor exactly at mid parameter", "[geometry][reshape]") {
    Path line;
    line.closed = false;
    line.nodes = {corner(0, 0), corner(9'000, 0)};
    const Path out = reshape_segment(line, 0, 0.5, pt(0, 1'200));
    Path seg = out;
    const Polyline flat = flatten(seg, Micrometers{2});
    // Le point du milieu (en x) est monté de ~1200 (cubique symétrique : y(0.5) = 1200).
    std::int32_t y = 0;
    for (const auto& p : flat.points) {
        if (std::abs(p.x.value - 4'500) < 60) {
            y = p.y.value;
        }
    }
    CHECK(std::abs(y - 1'200) <= 12);
}

TEST_CASE("reshape_segment keeps a smooth junction smooth", "[geometry][reshape]") {
    const Path chain = smooth_chain();
    const Path out = reshape_segment(chain, 0, 0.5, pt(0, 3'000));
    REQUIRE(out.nodes.size() == 3);
    const PathNode& b = out.nodes[1];
    REQUIRE(b.tan_in.has_value());
    REQUIRE(b.tan_out.has_value());
    // Tangentes colinéaires et opposées au nœud lisse B.
    const double cross = static_cast<double>(b.tan_in->x.value) * b.tan_out->y.value -
                         static_cast<double>(b.tan_in->y.value) * b.tan_out->x.value;
    const double dot = static_cast<double>(b.tan_in->x.value) * b.tan_out->x.value +
                       static_cast<double>(b.tan_in->y.value) * b.tan_out->y.value;
    CHECK(std::abs(cross) < 0.01 * std::abs(dot));
    CHECK(dot < 0.0);
    // Longueur de la poignée sortante conservée (3000).
    CHECK(std::abs(std::hypot(static_cast<double>(b.tan_out->x.value),
                              static_cast<double>(b.tan_out->y.value)) -
                   3'000.0) < 2.0);
    // Extrémités intactes.
    CHECK(out.nodes[0].pos == chain.nodes[0].pos);
    CHECK(out.nodes[2].pos == chain.nodes[2].pos);
}

TEST_CASE("reshape_segment is deterministic and ignores invalid segments", "[geometry][reshape]") {
    const Path sq = square(10'000);
    CHECK(reshape_segment(sq, 1, 0.4, pt(500, 700)) == reshape_segment(sq, 1, 0.4, pt(500, 700)));
    CHECK(reshape_segment(sq, 9, 0.5, pt(100, 100)) == sq);
    Path open = sq;
    open.closed = false;
    CHECK(reshape_segment(open, 3, 0.5, pt(100, 100)) == open); // pas de segment 3 sur un ouvert
    Path one;
    one.nodes = {corner(0, 0)};
    CHECK(reshape_segment(one, 0, 0.5, pt(1, 1)) == one);
}

TEST_CASE("repeated reshapes do not add nodes", "[geometry][reshape]") {
    Path p = square(10'000);
    for (int i = 0; i < 20; ++i) {
        p = reshape_segment(p, static_cast<std::size_t>(i % 4), 0.3 + 0.02 * i,
                            pt(100 * (i % 3) - 100, 80));
    }
    CHECK(p.nodes.size() == 4);
}

TEST_CASE("path_self_intersects detects a bow-tie and accepts a square", "[geometry][reshape]") {
    CHECK_FALSE(path_self_intersects(square(10'000), Micrometers{50}));
    Path bow;
    bow.closed = true;
    bow.nodes = {corner(0, 0), corner(10'000, 10'000), corner(10'000, 0), corner(0, 10'000)};
    CHECK(path_self_intersects(bow, Micrometers{50}));
}

TEST_CASE("reshape_path_set_segment refuses a pull that crosses itself", "[geometry][reshape]") {
    PathSet set;
    set.outer = square(10'000);
    // Tirer le bord bas à travers tout le carré fait croiser le bord haut.
    CHECK_FALSE(
        reshape_path_set_segment(set, 0, 0, 0.5, pt(0, 40'000), Micrometers{50}).has_value());
    // Un tirage raisonnable passe, et ne modifie que le chemin visé.
    const auto ok = reshape_path_set_segment(set, 0, 0, 0.5, pt(0, -2'000), Micrometers{50});
    REQUIRE(ok.has_value());
    CHECK(ok->outer != set.outer);
    // Index invalides.
    CHECK_FALSE(reshape_path_set_segment(set, 3, 0, 0.5, pt(0, 1), Micrometers{50}).has_value());
    CHECK_FALSE(reshape_path_set_segment(set, 0, 9, 0.5, pt(0, 1), Micrometers{50}).has_value());
}

TEST_CASE("reshape_path_set_segment works on a hole and refuses crossing the outer",
          "[geometry][reshape]") {
    PathSet set;
    set.outer = square(20'000);
    Path hole;
    hole.closed = true;
    hole.nodes = {corner(8'000, 8'000), corner(12'000, 8'000), corner(12'000, 12'000),
                  corner(8'000, 12'000)};
    set.holes.push_back(hole);
    const auto ok = reshape_path_set_segment(set, 1, 0, 0.5, pt(0, -1'500), Micrometers{50});
    REQUIRE(ok.has_value());
    CHECK(ok->outer == set.outer); // le contour extérieur n'est pas touché
    CHECK(ok->holes.size() == 1);
    CHECK(ok->holes[0].nodes.size() == 4);
    // Pousser le trou à travers le bord extérieur est refusé.
    CHECK_FALSE(
        reshape_path_set_segment(set, 1, 0, 0.5, pt(0, -30'000), Micrometers{50}).has_value());
}
