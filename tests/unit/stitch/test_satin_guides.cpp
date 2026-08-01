// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "openstitch/stitch_generation/satin_guides.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {
geometry::Path rail(std::int32_t y) {
    geometry::Path path;
    path.closed = false;
    path.nodes = {{{Micrometers{0}, Micrometers{y}}},
                  {{Micrometers{10'000}, Micrometers{y}}}};
    return path;
}

document::SatinParams satin() {
    document::SatinParams p;
    p.rail_a = rail(0);
    p.rail_b = rail(4'000);
    p.density = Micrometers{400};
    p.rungs = {{{Micrometers{0}, Micrometers{0}}, {Micrometers{0}, Micrometers{4'000}}},
               {{Micrometers{5'000}, Micrometers{0}}, {Micrometers{5'000}, Micrometers{4'000}}},
               {{Micrometers{10'000}, Micrometers{0}}, {Micrometers{10'000}, Micrometers{4'000}}}};
    return p;
}
}  // namespace

TEST_CASE("un guide satin deplace se projette exactement sur son rail") {
    const auto p = satin();
    const auto moved = move_satin_guide_endpoint(
        p, 1, SatinGuideSide::RailA, {Micrometers{6'000}, Micrometers{1'500}});
    REQUIRE(moved.has_value());
    CHECK(moved->a == Vec2um{Micrometers{6'000}, Micrometers{0}});
    CHECK(moved->b == p.rungs[1].b);
}

TEST_CASE("un guide satin ne peut pas franchir son voisin sur un seul rail") {
    const auto p = satin();
    CHECK_FALSE(move_satin_guide_endpoint(
                    p, 1, SatinGuideSide::RailA,
                    {Micrometers{9'950}, Micrometers{0}})
                    .has_value());
}

TEST_CASE("un index de guide satin obsolete est refuse") {
    const auto p = satin();
    CHECK_FALSE(move_satin_guide_endpoint(p, 99, SatinGuideSide::RailB, {}).has_value());
}

TEST_CASE("un nouveau guide satin partage le plus grand intervalle") {
    auto p = satin();
    p.rungs[1].a.x = Micrometers{2'000};
    p.rungs[1].b.x = Micrometers{2'000};
    const auto insertion = make_satin_guide_in_largest_gap(p);
    REQUIRE(insertion.has_value());
    CHECK(insertion->index == 2);
    CHECK(insertion->guide.a == Vec2um{Micrometers{6'000}, Micrometers{0}});
    CHECK(insertion->guide.b == Vec2um{Micrometers{6'000}, Micrometers{4'000}});
}

TEST_CASE("aucun guide satin ajoute dans un intervalle trop court") {
    auto p = satin();
    p.density = Micrometers{6'000};
    CHECK_FALSE(make_satin_guide_in_largest_gap(p).has_value());
}

TEST_CASE("un nouveau guide satin preserve un ordre de guides inverse") {
    auto p = satin();
    std::reverse(p.rungs.begin(), p.rungs.end());
    const auto insertion = make_satin_guide_in_largest_gap(p);
    REQUIRE(insertion.has_value());
    CHECK(insertion->index == 2);
    p.rungs.insert(p.rungs.begin() + static_cast<std::ptrdiff_t>(insertion->index),
                   insertion->guide);
    CHECK(p.rungs[0].a.x.value > p.rungs[1].a.x.value);
    CHECK(p.rungs[1].a.x.value > p.rungs[2].a.x.value);
}
