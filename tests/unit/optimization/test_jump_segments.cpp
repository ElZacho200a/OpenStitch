// SPDX-License-Identifier: Apache-2.0
// Surimpression « Sauts à vide » du panneau Ordre : segments entre la fin d'un objet et le début
// du suivant (calcul pur, ordre de couture conservé).
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "openstitch/optimization/order.hpp"

using namespace openstitch;
using namespace openstitch::optimization;

namespace {

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

} // namespace

TEST_CASE("jump_segments : un segment par paire d'objets successifs", "[optimization][jumps]") {
    const std::vector<SewnSpan> spans = {
        {pt(0, 0), pt(1000, 0)}, {pt(4000, 0), pt(4000, 3000)}, {pt(0, 3000), pt(500, 3000)}};
    const auto jumps = jump_segments(spans);
    REQUIRE(jumps.size() == 2);
    CHECK(jumps[0].from == pt(1000, 0));
    CHECK(jumps[0].to == pt(4000, 0));
    CHECK(jumps[1].from == pt(4000, 3000));
    CHECK(jumps[1].to == pt(0, 3000));
    // 3 mm puis 4 mm.
    CHECK(jump_length_um(jumps) == 7000);
}

TEST_CASE("jump_segments : cas limites et deplacements nuls omis", "[optimization][jumps]") {
    CHECK(jump_segments({}).empty());
    CHECK(jump_segments({{pt(0, 0), pt(1, 1)}}).empty());
    // La fin du premier objet est le debut du second : aucun saut.
    const auto none = jump_segments({{pt(0, 0), pt(500, 500)}, {pt(500, 500), pt(900, 900)}});
    CHECK(none.empty());
    CHECK(jump_length_um(none) == 0);
}

TEST_CASE("jump_segments : deterministe et longueur arrondie", "[optimization][jumps]") {
    const std::vector<SewnSpan> spans = {
        {pt(0, 0), pt(0, 0)}, {pt(3, 4), pt(3, 4)}, {pt(9, 12), pt(0, 0)}};
    const auto a = jump_segments(spans);
    const auto b = jump_segments(spans);
    CHECK(a == b);
    CHECK(jump_length_um(a) == 15); // 5 + 10
}
