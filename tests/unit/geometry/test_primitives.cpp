// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/primitives.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {
Vec2um p(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

// Aire signee (shoelace) d'une polyligne fermee brute (µm²).
double polyline_area_um2(const std::vector<Vec2um>& pts) {
    double area = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % pts.size()];
        area += static_cast<double>(a.x.value) * static_cast<double>(b.y.value) -
                static_cast<double>(b.x.value) * static_cast<double>(a.y.value);
    }
    return area * 0.5;
}
}  // namespace

// --- Rectangle ----------------------------------------------------------

TEST_CASE("rectangle_path : quatre coins droits, aire exacte") {
    const auto path = rectangle_path(p(0, 0), p(10'000, 5'000));
    REQUIRE(path.nodes.size() == 4);
    CHECK(path.closed);
    CHECK(std::abs(signed_area_um2(path)) == 50'000'000.0);  // 10 x 5 mm
    for (const auto& n : path.nodes) {
        CHECK(n.type == NodeType::Corner);
    }
}

TEST_CASE("rectangle_path : coins fournis dans n'importe quel ordre") {
    // Coin haut-droit puis bas-gauche : doit produire le meme rectangle.
    const auto a = rectangle_path(p(0, 0), p(10'000, 5'000));
    const auto b = rectangle_path(p(10'000, 5'000), p(0, 0));
    CHECK(std::abs(signed_area_um2(a)) == std::abs(signed_area_um2(b)));
}

TEST_CASE("rectangle_path : degenere (largeur nulle) sans crash") {
    const auto path = rectangle_path(p(0, 0), p(0, 5'000));
    REQUIRE(path.nodes.size() == 4);
    CHECK(signed_area_um2(path) == 0.0);
}

// --- Ellipse --------------------------------------------------------------

TEST_CASE("ellipse_path : quatre noeuds lisses, aire proche de pi*rx*ry") {
    // Ellipse 20 x 10 mm (rx=10mm, ry=5mm) centree a l'origine.
    const auto path = ellipse_path(p(-10'000, -5'000), p(10'000, 5'000));
    REQUIRE(path.nodes.size() == 4);
    CHECK(path.closed);
    for (const auto& n : path.nodes) {
        CHECK(n.type == NodeType::Smooth);
        CHECK(n.tan_in.has_value());
        CHECK(n.tan_out.has_value());
    }
    const auto flat = flatten(path, Micrometers{20});
    const double area = std::abs(polyline_area_um2(flat.points)) / 1e6;  // mm^2
    const double expected = std::numbers::pi * 10.0 * 5.0;
    CHECK(std::abs(area - expected) / expected < 0.01);  // < 1%
}

TEST_CASE("ellipse_path : cercle quand les deux cotes sont egaux") {
    const auto path = ellipse_path(p(-5'000, -5'000), p(5'000, 5'000));
    const auto flat = flatten(path, Micrometers{10});
    // Chaque point aplati doit etre a ~5mm (rayon) du centre.
    for (const auto& pt : flat.points) {
        const double r = length_um(pt) / 1000.0;  // mm
        CHECK(std::abs(r - 5.0) < 0.05);
    }
}

TEST_CASE("ellipse_path : coins fournis dans n'importe quel ordre") {
    const auto a = ellipse_path(p(-10'000, -5'000), p(10'000, 5'000));
    const auto b = ellipse_path(p(10'000, 5'000), p(-10'000, -5'000));
    const auto flatA = flatten(a, Micrometers{20});
    const auto flatB = flatten(b, Micrometers{20});
    const double areaA = std::abs(polyline_area_um2(flatA.points));
    const double areaB = std::abs(polyline_area_um2(flatB.points));
    CHECK(std::abs(areaA - areaB) < 1.0);
}

TEST_CASE("ellipse_path : degenere (rayon nul) sans crash") {
    const auto path = ellipse_path(p(0, 0), p(0, 5'000));
    REQUIRE(path.nodes.size() == 4);
    const auto flat = flatten(path, Micrometers{20});
    CHECK_FALSE(flat.points.empty());
}

// --- Polygone ---------------------------------------------------------------

TEST_CASE("polygon_path : sommets relies dans l'ordre, aire exacte") {
    // Triangle rectangle 10 x 5 mm -> aire 25 mm^2.
    const auto path = polygon_path({p(0, 0), p(10'000, 0), p(0, 5'000)});
    REQUIRE(path.nodes.size() == 3);
    CHECK(path.closed);
    CHECK(std::abs(signed_area_um2(path)) == 25'000'000.0);
    for (const auto& n : path.nodes) {
        CHECK(n.type == NodeType::Corner);
    }
}

TEST_CASE("polygon_path : moins de trois sommets -> chemin vide") {
    CHECK(polygon_path({}).nodes.empty());
    CHECK(polygon_path({p(0, 0)}).nodes.empty());
    CHECK(polygon_path({p(0, 0), p(1'000, 0)}).nodes.empty());
}

TEST_CASE("polygon_path : deterministe") {
    const std::vector<Vec2um> verts = {p(0, 0), p(10'000, 0), p(10'000, 10'000), p(0, 10'000),
                                       p(-5'000, 5'000)};
    CHECK(polygon_path(verts).nodes == polygon_path(verts).nodes);
}

// --- Polygone regulier ---------------------------------------------------

TEST_CASE("regular_polygon_path : hexagone, six sommets tous a distance = rayon") {
    const auto path = regular_polygon_path(p(0, 0), Micrometers{10'000}, 6);
    REQUIRE(path.nodes.size() == 6);
    CHECK(path.closed);
    for (const auto& n : path.nodes) {
        CHECK(n.type == NodeType::Corner);
        const double dx = static_cast<double>(n.pos.x.value);
        const double dy = static_cast<double>(n.pos.y.value);
        CHECK(std::abs(std::sqrt(dx * dx + dy * dy) - 10'000.0) < 1.0);
    }
}

TEST_CASE("regular_polygon_path : angle de depart par defaut place le premier sommet au nord") {
    const auto path = regular_polygon_path(p(0, 0), Micrometers{10'000}, 4);
    REQUIRE(path.nodes.size() == 4);
    // -pi/2 (nord, repere Y vers le haut) : premier sommet en (0, rayon).
    CHECK(std::abs(path.nodes.front().pos.x.value) < 1);
    CHECK(std::abs(path.nodes.front().pos.y.value - 10'000) < 1);
}

TEST_CASE("regular_polygon_path : angles regulierement espaces") {
    const auto path = regular_polygon_path(p(0, 0), Micrometers{10'000}, 5, 0.0);
    REQUIRE(path.nodes.size() == 5);
    for (std::size_t i = 0; i < path.nodes.size(); ++i) {
        const double expected = i * 2.0 * std::numbers::pi / 5.0;
        const double x = 10'000.0 * std::cos(expected);
        const double y = 10'000.0 * std::sin(expected);
        CHECK(std::abs(static_cast<double>(path.nodes[i].pos.x.value) - x) < 1.0);
        CHECK(std::abs(static_cast<double>(path.nodes[i].pos.y.value) - y) < 1.0);
    }
}

TEST_CASE("regular_polygon_path : moins de trois cotes ou rayon nul -> chemin vide") {
    CHECK(regular_polygon_path(p(0, 0), Micrometers{10'000}, 2).nodes.empty());
    CHECK(regular_polygon_path(p(0, 0), Micrometers{0}, 6).nodes.empty());
}

TEST_CASE("regular_polygon_path : deterministe") {
    const auto a = regular_polygon_path(p(1'000, 2'000), Micrometers{7'500}, 8);
    const auto b = regular_polygon_path(p(1'000, 2'000), Micrometers{7'500}, 8);
    CHECK(a.nodes == b.nodes);
}

// --- Forme libre (main levee / lasso) ----------------------------------------

TEST_CASE("freeform_path : simplifie un trace bruite en contour exploitable") {
    // Trace approximatif d'un carre 10 x 10 mm, avec plusieurs points
    // colineaires intermediaires sur chaque bord (comme un glisser souris
    // reel, un point par evenement de deplacement).
    const std::vector<Vec2um> stroke = {
        p(0, 0),     p(2'000, 0),     p(4'000, 0),     p(6'000, 0),
        p(8'000, 0), p(10'000, 0),    p(10'000, 2'000), p(10'000, 4'000),
        p(10'000, 6'000), p(10'000, 8'000), p(10'000, 10'000),
        p(8'000, 10'000), p(6'000, 10'000), p(4'000, 10'000), p(2'000, 10'000),
        p(0, 10'000), p(0, 8'000), p(0, 6'000), p(0, 4'000), p(0, 2'000),
    };
    const auto path = freeform_path(stroke, Micrometers{300});
    REQUIRE(path.nodes.size() >= 3);
    CHECK(path.nodes.size() < stroke.size());  // simplification reelle
    CHECK(path.closed);
    for (const auto& n : path.nodes) {
        CHECK(n.type == NodeType::Corner);
    }
    // Aire proche de 10 x 10 mm = 100 000 000 um^2 (tolerance : arrondi de
    // simplification pres des coins).
    const double area = std::abs(signed_area_um2(path));
    CHECK(area > 90'000'000.0);
    CHECK(area < 110'000'000.0);
}

TEST_CASE("freeform_path : moins de trois points -> chemin vide") {
    CHECK(freeform_path({}, Micrometers{300}).nodes.empty());
    CHECK(freeform_path({p(0, 0)}, Micrometers{300}).nodes.empty());
    CHECK(freeform_path({p(0, 0), p(1'000, 0)}, Micrometers{300}).nodes.empty());
}

TEST_CASE("freeform_path : points colineaires -> chemin vide apres simplification") {
    // Trois points alignes (et plus) ne forment jamais une forme fermee
    // exploitable : la simplification les reduit a moins de 3 sommets.
    const std::vector<Vec2um> collinear = {p(0, 0), p(1'000, 0), p(2'000, 0), p(3'000, 0)};
    CHECK(freeform_path(collinear, Micrometers{300}).nodes.empty());
}

TEST_CASE("freeform_path : deterministe") {
    const std::vector<Vec2um> stroke = {p(0, 0), p(5'000, 100), p(10'000, 0), p(10'000, 10'000),
                                        p(5'000, 9'900), p(0, 10'000)};
    CHECK(freeform_path(stroke, Micrometers{300}).nodes ==
          freeform_path(stroke, Micrometers{300}).nodes);
}
