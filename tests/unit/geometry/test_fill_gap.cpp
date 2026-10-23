// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/fill_gap.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

Vec2um p(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

PathNode corner(Vec2um pos) {
    return PathNode{pos, NodeType::Corner, std::nullopt, std::nullopt};
}

// Rectangle [x0,x1] x [y0,y1] en millimètres.
PathSet block(int x0, int y0, int x1, int y1, int scale = 1000) {
    Path path;
    path.closed = true;
    path.nodes = {corner(p(x0 * scale, y0 * scale)), corner(p(x1 * scale, y0 * scale)),
                  corner(p(x1 * scale, y1 * scale)), corner(p(x0 * scale, y1 * scale))};
    return {path, {}};
}

// Quatre blocs autour d'un vide carré de 10 mm de côté (10..20 x 10..20).
std::vector<PathSet> ring() {
    return {block(0, 0, 10, 30), block(20, 0, 30, 30), block(10, 0, 20, 10), block(10, 20, 20, 30)};
}

} // namespace

TEST_CASE("fill_gap_at fills the closed hole between several shapes", "[geometry][fill_gap]") {
    const auto gap = fill_gap_at(ring(), p(15'000, 15'000));
    REQUIRE(gap.has_value());
    CHECK(gap->holes.empty());
    CHECK_THAT(path_set_area_um2(*gap), Catch::Matchers::WithinAbs(1.0e8, 1.0));
}

TEST_CASE("fill_gap_at leaves enclosed islands as holes", "[geometry][fill_gap]") {
    auto shapes = ring();
    shapes.push_back(block(13, 13, 17, 17));
    const auto gap = fill_gap_at(shapes, p(11'000, 11'000));
    REQUIRE(gap.has_value());
    REQUIRE(gap->holes.size() == 1);
    CHECK_THAT(path_set_area_um2(*gap), Catch::Matchers::WithinAbs(1.0e8 - 16.0e6, 1.0));
}

TEST_CASE("fill_gap_at refuses a seed lying on a shape", "[geometry][fill_gap]") {
    const auto gap = fill_gap_at(ring(), p(5'000, 15'000));
    REQUIRE(!gap.has_value());
    CHECK(gap.error().category == ErrorCategory::OperationImpossible);
}

TEST_CASE("fill_gap_at refuses an open area", "[geometry][fill_gap]") {
    auto shapes = ring();
    shapes.pop_back(); // le haut est ouvert : le vide rejoint l'extérieur
    const auto gap = fill_gap_at(shapes, p(15'000, 15'000));
    REQUIRE(!gap.has_value());
    CHECK(gap.error().category == ErrorCategory::OperationImpossible);
    CHECK(gap.error().message.find("fermée") != std::string::npos);
}

TEST_CASE("fill_gap_at refuses an empty drawing", "[geometry][fill_gap]") {
    CHECK(!fill_gap_at({}, p(0, 0)).has_value());
}

TEST_CASE("fill_gap_at grow seals a thin slit and restores the filled area",
          "[geometry][fill_gap]") {
    // Fente de 0,3 mm entre le bloc de gauche et celui du haut : le vide fuit sans tolérance.
    std::vector<PathSet> shapes = {block(0, 0, 10, 30), block(20, 0, 30, 30), block(10, 0, 20, 10)};
    PathSet top;
    top.outer.closed = true;
    top.outer.nodes = {corner(p(10'300, 20'000)), corner(p(20'000, 20'000)),
                       corner(p(20'000, 30'000)), corner(p(10'300, 30'000))};
    shapes.push_back(top);
    CHECK(!fill_gap_at(shapes, p(15'000, 15'000)).has_value());

    const auto sealed = fill_gap_at(shapes, p(15'000, 15'000), Micrometers{200});
    REQUIRE(sealed.has_value());
    // Le vide de 10 x 10 mm retrouve (à l'arrondi des angles près) son aire d'origine.
    CHECK_THAT(path_set_area_um2(*sealed), Catch::Matchers::WithinRel(1.0e8, 0.01));
}

TEST_CASE("fill_gap_at is deterministic", "[geometry][fill_gap]") {
    const auto a = fill_gap_at(ring(), p(12'000, 18'000));
    const auto b = fill_gap_at(ring(), p(12'000, 18'000));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == *b);
}
