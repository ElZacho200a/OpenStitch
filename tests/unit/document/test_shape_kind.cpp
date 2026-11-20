// SPDX-License-Identifier: Apache-2.0
// Lignes ouvertes : prédicat de forme et types de points qui demandent une surface.
#include <catch2/catch_test_macros.hpp>

#include "openstitch/document/shape_kind.hpp"

using namespace openstitch;

namespace {

geometry::Path polyline(std::initializer_list<std::pair<int, int>> pts, bool closed) {
    geometry::Path p;
    p.closed = closed;
    for (const auto& [x, y] : pts) {
        geometry::PathNode n;
        n.pos = Vec2um{Micrometers{x}, Micrometers{y}};
        p.nodes.push_back(n);
    }
    return p;
}

} // namespace

TEST_CASE("shape kind: open line needs every piece open and hole free") {
    document::VectorObject v;
    CHECK_FALSE(document::is_open_line(v)); // vide
    v.paths.push_back({polyline({{0, 0}, {10'000, 0}, {10'000, 5'000}}, false), {}});
    CHECK(document::is_open_line(v));
    v.paths.push_back({polyline({{0, 0}, {5'000, 5'000}}, false), {}});
    CHECK(document::is_open_line(v));
    // Un morceau fermé : forme mixte, traitée comme une surface (comportement historique).
    v.paths.push_back({polyline({{0, 0}, {1'000, 0}, {1'000, 1'000}}, true), {}});
    CHECK_FALSE(document::is_open_line(v));
    // Un seul point : pas une ligne.
    document::VectorObject dot;
    dot.paths.push_back({polyline({{0, 0}}, false), {}});
    CHECK_FALSE(document::is_open_line(dot));
}

TEST_CASE("shape kind: only path following stitches work without an area") {
    CHECK_FALSE(document::stitch_needs_area(document::RunningStitchParams{}));
    CHECK_FALSE(document::stitch_needs_area(document::BlanketStitchParams{}));
    CHECK_FALSE(document::stitch_needs_area(document::MotifRunParams{}));
    CHECK_FALSE(document::stitch_needs_area(document::SatinParams{}));
    CHECK(document::stitch_needs_area(document::TatamiParams{}));
    CHECK(document::stitch_needs_area(document::AutoSatinParams{}));
    CHECK(document::stitch_needs_area(document::DirectionalFillParams{}));
    CHECK(document::stitch_needs_area(document::ContourFillParams{}));
    CHECK(document::stitch_needs_area(document::CrossStitchParams{}));
    CHECK(document::stitch_needs_area(document::AppliqueParams{}));
}
