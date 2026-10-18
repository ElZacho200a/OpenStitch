// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/geometry/to_curves.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

Vec2um pt(double x, double y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(y))}};
}

PathNode corner_node(double x, double y) {
    return PathNode{pt(x, y), NodeType::Corner, std::nullopt, std::nullopt};
}

std::size_t curved_nodes(const Path& p) {
    std::size_t n = 0;
    for (const auto& nd : p.nodes) {
        n += (nd.tan_in || nd.tan_out) ? 1U : 0U;
    }
    return n;
}

} // namespace

TEST_CASE("convert_to_curves : un carre garde ses coins") {
    const Path square = rectangle_path(pt(0, 0), pt(10'000, 10'000));
    CHECK(convert_to_curves(square) == square);
}

TEST_CASE("convert_to_curves : passe par les sommets et lisse un octogone") {
    const Path octagon = regular_polygon_path(pt(0, 0), Micrometers{10'000}, 8);
    ToCurvesOptions options;
    options.corner_angle_deg = 90.0;
    const Path curved = convert_to_curves(octagon, options);
    REQUIRE(curved.nodes.size() == octagon.nodes.size());
    for (std::size_t i = 0; i < octagon.nodes.size(); ++i) {
        CHECK(curved.nodes[i].pos == octagon.nodes[i].pos);
        CHECK(curved.nodes[i].type == NodeType::Smooth);
        REQUIRE(curved.nodes[i].tan_in.has_value());
        REQUIRE(curved.nodes[i].tan_out.has_value());
        // Tangentes colineaires (produit vectoriel quasi nul, a l'arrondi pres).
        const double cross =
            static_cast<double>(curved.nodes[i].tan_in->x.value) *
                curved.nodes[i].tan_out->y.value -
            static_cast<double>(curved.nodes[i].tan_in->y.value) * curved.nodes[i].tan_out->x.value;
        CHECK(std::abs(cross) < 2.0e4);
    }
    // La courbe differe du polygone (plus de points une fois aplatie) et reste
    // dans le cercle circonscrit a une marge pres.
    const Path flat = flatten_to_polygon(curved, Micrometers{20});
    CHECK(flat.nodes.size() > octagon.nodes.size());
    for (const auto& n : flat.nodes) {
        CHECK(std::hypot(static_cast<double>(n.pos.x.value), static_cast<double>(n.pos.y.value)) <=
              10'050.0);
    }
}

TEST_CASE("convert_to_curves : coins francs conserves, extremites ouvertes en coin") {
    Path open;
    open.closed = false;
    open.nodes = {corner_node(0, 0), corner_node(5'000, 100), corner_node(10'000, 0),
                  corner_node(10'000, 8'000)};
    const Path curved = convert_to_curves(open);
    REQUIRE(curved.nodes.size() == 4);
    CHECK(curved.nodes[0].type == NodeType::Corner);
    CHECK_FALSE(curved.nodes[0].tan_out.has_value());
    CHECK(curved.nodes[1].type == NodeType::Smooth); // petit braquage
    CHECK(curved.nodes[2].type == NodeType::Corner); // braquage ~ 90 degres
    CHECK(curved.nodes[3].type == NodeType::Corner);
}

TEST_CASE("convert_to_curves : deterministe et ensemble de chemins") {
    PathSet set;
    set.outer = regular_polygon_path(pt(0, 0), Micrometers{10'000}, 12);
    set.holes.push_back(regular_polygon_path(pt(0, 0), Micrometers{3'000}, 12));
    const PathSet a = convert_to_curves(set);
    const PathSet b = convert_to_curves(set);
    CHECK(a == b);
    CHECK(curved_nodes(a.outer) == 12);
    REQUIRE(a.holes.size() == 1);
    CHECK(curved_nodes(a.holes[0]) == 12);
}
