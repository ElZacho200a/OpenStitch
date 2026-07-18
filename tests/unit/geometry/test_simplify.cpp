// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/geometry/simplify.hpp"

using namespace openstitch;
using namespace openstitch::geometry;

namespace {

PathNode node(std::int32_t x, std::int32_t y) {
    return PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, NodeType::Corner, std::nullopt,
                    std::nullopt};
}

}  // namespace

TEST_CASE("simplify : points colineaires supprimes") {
    Path path;
    path.closed = false;
    for (std::int32_t x = 0; x <= 10'000; x += 1'000) {
        path.nodes.push_back(node(x, 0));
    }
    const Path out = simplify(path, Micrometers{50});
    CHECK(out.nodes.size() == 2);
    CHECK(out.nodes.front().pos.x.value == 0);
    CHECK(out.nodes.back().pos.x.value == 10'000);
}

TEST_CASE("simplify : un vrai coin est conserve") {
    Path path;
    path.closed = false;
    path.nodes = {node(0, 0), node(5'000, 0), node(5'000, 5'000)};
    const Path out = simplify(path, Micrometers{100});
    CHECK(out.nodes.size() == 3);
}

TEST_CASE("simplify : ecart sous tolerance aplati, au-dessus conserve") {
    Path path;
    path.closed = false;
    path.nodes = {node(0, 0), node(5'000, 150), node(10'000, 0)};
    CHECK(simplify(path, Micrometers{200}).nodes.size() == 2);  // 150 < 200 : aplati
    CHECK(simplify(path, Micrometers{100}).nodes.size() == 3);  // 150 > 100 : conserve
}

TEST_CASE("simplify : chemin ferme, cas minimaux intacts") {
    Path triangle;
    triangle.closed = true;
    triangle.nodes = {node(0, 0), node(10'000, 0), node(5'000, 8'000)};
    CHECK(simplify(triangle, Micrometers{500}).nodes.size() == 3);

    Path segment;
    segment.closed = false;
    segment.nodes = {node(0, 0), node(1'000, 0)};
    CHECK(simplify(segment, Micrometers{500}).nodes.size() == 2);
}

TEST_CASE("simplify : rectangle ferme bruite -> 4 coins") {
    Path path;
    path.closed = true;
    // Rectangle 10x6 mm avec un point intermediaire bruite (50 um) par cote.
    path.nodes = {node(0, 0),      node(5'000, 50),     node(10'000, 0), node(10'000, 3'000),
                  node(10'000, 6'000), node(5'000, 5'950), node(0, 6'000), node(0, 3'000)};
    const Path out = simplify(path, Micrometers{200});
    CHECK(out.nodes.size() == 4);
}

TEST_CASE("aire signee : carre antihoraire positive") {
    Path square;
    square.closed = true;
    square.nodes = {node(0, 0), node(10'000, 0), node(10'000, 10'000), node(0, 10'000)};
    CHECK(signed_area_um2(square) == 100'000'000.0);  // 10 mm x 10 mm en µm²
    std::reverse(square.nodes.begin(), square.nodes.end());
    CHECK(signed_area_um2(square) == -100'000'000.0);
}
