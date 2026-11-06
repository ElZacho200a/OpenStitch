// SPDX-License-Identifier: Apache-2.0
#include "radial_menu.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace openstitch::desktop_v2;

TEST_CASE("radial sectors turn clockwise from the top", "[radial_menu]") {
    CHECK(radialSectorAt(0, -50, 10, 8) == 0);  // haut
    CHECK(radialSectorAt(50, -50, 10, 8) == 1); // haut-droite
    CHECK(radialSectorAt(50, 0, 10, 8) == 2);   // droite
    CHECK(radialSectorAt(0, 50, 10, 8) == 4);   // bas
    CHECK(radialSectorAt(-50, 0, 10, 8) == 6);  // gauche
    CHECK(radialSectorAt(-1, -50, 10, 8) == 0); // juste à gauche du haut : encore le secteur 0
    CHECK(radialSectorAt(0, -50, 10, 4) == 0);
    CHECK(radialSectorAt(50, 0, 10, 4) == 1);
}

TEST_CASE("radial centre disc and invalid counts select nothing", "[radial_menu]") {
    CHECK(radialSectorAt(3, 3, 10, 8) == -1);
    CHECK(radialSectorAt(0, -50, 10, 0) == -1);
    CHECK(radialSectorAt(0, -50, 10, 9) == -1);
}

TEST_CASE("radial keyboard alternatives", "[radial_menu]") {
    CHECK(radialNeighbour(-1, +1, 5) == 0);
    CHECK(radialNeighbour(-1, -1, 5) == 4);
    CHECK(radialNeighbour(4, +1, 5) == 0);
    CHECK(radialNeighbour(0, -1, 5) == 4);
    CHECK(radialNeighbour(0, +1, 0) == -1);
    CHECK(radialSectorForDigit(1, 5) == 0);
    CHECK(radialSectorForDigit(5, 5) == 4);
    CHECK(radialSectorForDigit(6, 5) == -1);
    CHECK(radialSectorForDigit(0, 5) == -1);
}
