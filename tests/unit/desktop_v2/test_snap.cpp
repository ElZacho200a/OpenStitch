// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "snap.hpp"

using namespace openstitch::desktop_v2;

namespace {
const SnapBox kMoving{40, 0, 60, 20}; // 20 x 20, à droite
const SnapBox kFixed{-10, 0, 10, 20}; // 20 x 20, centré sur l'axe
} // namespace

TEST_CASE("an edge within the threshold snaps onto the neighbour's edge", "[desktop_v2][snap]") {
    // Amener le bord gauche (40) sur le bord droit du voisin (10) demande -30 ; -29,7 s'y accroche.
    const auto r = snapMove(kMoving, {-29.7, 0.0}, {kFixed}, 1.0, false);
    CHECK(r.snappedX);
    CHECK(std::abs(r.delta.x - (-30.0)) < 1e-9);
    CHECK_FALSE(r.guides.empty());
    CHECK(r.guides.front().vertical);
    CHECK(std::abs(r.guides.front().positionMm - 10.0) < 1e-9);
}

TEST_CASE("centres snap together too", "[desktop_v2][snap]") {
    // Centre mobile 50 ; centre fixe 0 : décalage -50 ; -49,6 s'y accroche.
    const auto r = snapMove(kMoving, {-49.6, 7.0}, {kFixed}, 1.0, false);
    CHECK(r.snappedX);
    CHECK(std::abs(r.delta.x - (-50.0)) < 1e-9);
}

TEST_CASE("beyond the threshold nothing moves", "[desktop_v2][snap]") {
    const auto r = snapMove(kMoving, {-25.0, 3.0}, {kFixed}, 1.0, false);
    CHECK_FALSE(r.snappedX);
    CHECK(r.delta.x == -25.0);
    CHECK(r.guides.empty());
}

TEST_CASE("both axes can snap at once and report two guides", "[desktop_v2][snap]") {
    // Bord haut mobile 20 -> y1 ; cible y1 = 20 : delta.y = 0,3 -> accroche à 0.
    const auto r = snapMove(kMoving, {-29.8, 0.3}, {kFixed}, 1.0, false);
    CHECK(r.snappedX);
    CHECK(r.snappedY);
    CHECK(std::abs(r.delta.y) < 1e-9);
    CHECK(r.guides.size() == 2);
}

TEST_CASE("a non-positive threshold disables snapping (Alt held)", "[desktop_v2][snap]") {
    const auto r = snapMove(kMoving, {-29.7, 0.3}, {kFixed}, 0.0, true);
    CHECK_FALSE(r.snappedX);
    CHECK_FALSE(r.snappedY);
    CHECK(r.delta.x == -29.7);
    CHECK(r.delta.y == 0.3);
}

TEST_CASE("the motif axis is a target when asked", "[desktop_v2][snap]") {
    const SnapBox box{-5, -5, 5, 5};
    const auto r = snapMove(box, {0.4, -0.3}, {}, 1.0, true);
    CHECK(r.snappedX);
    CHECK(r.snappedY);
    CHECK(std::abs(r.delta.x) < 1e-9); // centre sur x = 0
    CHECK(std::abs(r.delta.y) < 1e-9);
    const auto without = snapMove(box, {0.4, -0.3}, {}, 1.0, false);
    CHECK_FALSE(without.snappedX);
}

TEST_CASE("the nearest line wins and the result is deterministic", "[desktop_v2][snap]") {
    const SnapBox other{12, 0, 32, 20};
    const auto a = snapMove(kMoving, {-29.9, 0.0}, {kFixed, other}, 3.0, false);
    const auto b = snapMove(kMoving, {-29.9, 0.0}, {kFixed, other}, 3.0, false);
    CHECK(a.delta.x == b.delta.x);
    CHECK(a.snappedX);
    // La ligne x = 10 est à 0,1 mm, x = 12 à 1,9 mm : la plus proche gagne.
    CHECK(std::abs(a.delta.x - (-30.0)) < 1e-9);
}
