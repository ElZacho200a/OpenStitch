// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include "openstitch/geometry/measure.hpp"
#include "openstitch/geometry/primitives.hpp"

using namespace openstitch;
using namespace openstitch::geometry;
using Catch::Approx;

namespace {
Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}
} // namespace

TEST_CASE("measure_segment : distance, deltas, direction et mm") {
    const SegmentMeasure m = measure_segment(pt(1'000, 2'000), pt(4'000, 6'000));
    CHECK(m.dx.value == 3'000);
    CHECK(m.dy.value == 4'000);
    CHECK(m.length_um == Approx(5'000.0));
    CHECK(m.length_mm().value == Approx(5.0));
    CHECK(m.dx_mm().value == Approx(3.0));
    CHECK(m.dy_mm().value == Approx(4.0));
    CHECK(m.direction.radians == Approx(std::atan2(4.0, 3.0)));
    CHECK_FALSE(m.is_degenerate());
}

TEST_CASE("measure_segment : directions cardinales et point confondu") {
    CHECK(measure_segment(pt(0, 0), pt(10, 0)).direction.radians == Approx(0.0));
    CHECK(measure_segment(pt(0, 0), pt(0, 10)).direction.radians == Approx(std::numbers::pi / 2));
    CHECK(measure_segment(pt(0, 0), pt(-10, 0)).direction.radians == Approx(std::numbers::pi));
    CHECK(measure_segment(pt(0, 0), pt(0, -10)).direction.radians == Approx(-std::numbers::pi / 2));
    const SegmentMeasure same = measure_segment(pt(7, 7), pt(7, 7));
    CHECK(same.is_degenerate());
    CHECK(same.length_um == 0.0);
    CHECK(distance_um(pt(0, 0), pt(30, 40)) == Approx(50.0));
}

TEST_CASE("polyline_length_um et path_length_um : ouvert, ferme, courbe") {
    const std::vector<Vec2um> pts{pt(0, 0), pt(3'000, 0), pt(3'000, 4'000)};
    CHECK(polyline_length_um(pts, false) == Approx(7'000.0));
    CHECK(polyline_length_um(pts, true) == Approx(12'000.0)); // + hypotenuse 5000
    CHECK(polyline_length_um({pt(0, 0)}, false) == 0.0);

    const Path rect = rectangle_path(pt(0, 0), pt(10'000, 5'000));
    CHECK(path_length_um(rect, Micrometers{10}) == Approx(30'000.0));
    Path open = rect;
    open.closed = false;
    CHECK(path_length_um(open, Micrometers{10}) == Approx(25'000.0));

    // Cercle : perimetre 2 pi r a la tolerance pres.
    const Path circle = ellipse_path(pt(-10'000, -10'000), pt(10'000, 10'000));
    CHECK(path_length_um(circle, Micrometers{2}) ==
          Approx(2.0 * std::numbers::pi * 10'000.0).epsilon(0.001));
}

TEST_CASE("angle_between : droit, plat, aigu, degenere") {
    const Vec2um v = pt(0, 0);
    CHECK(angle_between(v, pt(10, 0), pt(0, 10))->radians == Approx(std::numbers::pi / 2));
    CHECK(angle_between(v, pt(10, 0), pt(0, 10))->radians ==
          Approx(angle_between(v, pt(0, 10), pt(10, 0))->radians));
    CHECK(angle_between(v, pt(10, 0), pt(-10, 0))->radians == Approx(std::numbers::pi));
    CHECK(angle_between(v, pt(10, 0), pt(20, 0))->radians == Approx(0.0).margin(1e-12));
    CHECK(angle_between(v, pt(10, 0), pt(10, 10))->radians == Approx(std::numbers::pi / 4));
    CHECK_FALSE(angle_between(v, pt(0, 0), pt(10, 0)).has_value());
    CHECK_FALSE(angle_between(v, pt(10, 0), pt(0, 0)).has_value());
}

TEST_CASE("polygon_area_um2 et path_area_um2 : aires et conversion mm2") {
    const std::vector<Vec2um> square{pt(0, 0), pt(10'000, 0), pt(10'000, 10'000), pt(0, 10'000)};
    CHECK(polygon_area_um2(square) == Approx(1.0e8));
    CHECK(area_to_mm2(polygon_area_um2(square)).value == Approx(100.0));
    // Orientation indifferente.
    const std::vector<Vec2um> reversed(square.rbegin(), square.rend());
    CHECK(polygon_area_um2(reversed) == Approx(1.0e8));
    CHECK(polygon_area_um2({pt(0, 0), pt(10, 0)}) == 0.0);

    const Path circle = ellipse_path(pt(-10'000, -10'000), pt(10'000, 10'000));
    CHECK(path_area_um2(circle, Micrometers{2}) == Approx(std::numbers::pi * 1.0e8).epsilon(0.001));
    Path open = rectangle_path(pt(0, 0), pt(10, 10));
    open.closed = false;
    CHECK(path_area_um2(open, Micrometers{1}) == 0.0);
}
