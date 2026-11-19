// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "lasso_select.hpp"

using namespace openstitch::desktop_v2;
using openstitch::stitch_render::RectMm;

namespace {

std::vector<PointMm> square(double x0, double y0, double x1, double y1) {
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

const std::map<std::uint64_t, RectMm> kBoxes{
    {1, {10, 10, 20, 20}},   // petit, au centre
    {2, {30, 10, 40, 20}},   // a droite
    {3, {-50, -50, 50, 50}}, // tres grand, contient tout
};

} // namespace

TEST_CASE("lasso : point dans un polygone, concave compris", "[desktop_v2][lasso]") {
    const std::vector<PointMm> u{{0, 0}, {10, 0}, {10, 10}, {6, 10},
                                 {6, 4}, {4, 4},  {4, 10},  {0, 10}};
    CHECK(pointInPolygon({2, 5}, u));
    CHECK(pointInPolygon({8, 5}, u));
    CHECK_FALSE(pointInPolygon({5, 8}, u)); // dans l'encoche du U
    CHECK_FALSE(pointInPolygon({20, 5}, u));
}

TEST_CASE("lasso : segment et rectangle", "[desktop_v2][lasso]") {
    const RectMm box{10, 10, 20, 20};
    CHECK(segmentTouchesRect({0, 15}, {30, 15}, box));  // traverse
    CHECK(segmentTouchesRect({12, 12}, {14, 14}, box)); // entierement dedans
    CHECK(segmentTouchesRect({20, 20}, {30, 30}, box)); // touche un coin
    CHECK_FALSE(segmentTouchesRect({0, 0}, {5, 30}, box));
    CHECK_FALSE(segmentTouchesRect({0, 25}, {30, 25}, box));
}

TEST_CASE("lasso : mode englobe, seuls les objets entierement dedans", "[desktop_v2][lasso]") {
    const auto ids = lassoSelect(square(5, 5, 25, 25), kBoxes, LassoMode::Enclosed);
    REQUIRE(ids.size() == 1);
    CHECK(ids.front() == 1);
}

TEST_CASE("lasso : mode touche, objets traverses ou contenant le lasso", "[desktop_v2][lasso]") {
    // Le lasso deborde sur l'objet 2 et se trouve DANS l'objet 3.
    const auto ids = lassoSelect(square(5, 5, 32, 25), kBoxes, LassoMode::Touching);
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == 1);
    CHECK(ids[1] == 2);
    CHECK(ids[2] == 3);
    const std::map<std::uint64_t, RectMm> few{{1, {10, 10, 20, 20}}};
    CHECK(lassoSelect(square(100, 100, 120, 120), few, LassoMode::Touching).empty());
}

TEST_CASE("lasso : un lasso en C dont l'ouverture traverse la boite n'englobe pas",
          "[desktop_v2][lasso]") {
    // C ouvert a droite autour de la boite 1 : l'ouverture laisse passer un bord.
    const std::vector<PointMm> c{{0, 0},   {30, 0},  {30, 12}, {15, 12},
                                 {15, 18}, {30, 18}, {30, 30}, {0, 30}};
    const std::map<std::uint64_t, RectMm> boxes{{1, {10, 10, 20, 20}}};
    CHECK(lassoSelect(c, boxes, LassoMode::Enclosed).empty());
    CHECK(lassoSelect(c, boxes, LassoMode::Touching).size() == 1);
}

TEST_CASE("lasso : filtre d'acceptation et polygone trop court", "[desktop_v2][lasso]") {
    const auto ids = lassoSelect(square(-100, -100, 100, 100), kBoxes, LassoMode::Enclosed,
                                 [](std::uint64_t id) { return id != 2; });
    REQUIRE(ids.size() == 2);
    CHECK(ids[0] == 1);
    CHECK(ids[1] == 3);
    CHECK(lassoSelect({{0, 0}, {50, 50}}, kBoxes, LassoMode::Touching).empty());
    CHECK(lassoSelect({}, kBoxes, LassoMode::Touching).empty());
}
