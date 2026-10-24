// SPDX-License-Identifier: Apache-2.0
// Remplissage concentrique / contour (HP-STI-007).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "openstitch/stitch_generation/contour_fill.hpp"
#include "openstitch/stitch_generation/generate.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

Vec2um um(double xMm, double yMm) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(xMm * 1000.0))},
                  Micrometers{static_cast<std::int32_t>(std::lround(yMm * 1000.0))}};
}

geometry::Path polygon(const std::vector<Vec2um>& pts) {
    geometry::Path p;
    p.closed = true;
    for (const Vec2um v : pts) {
        p.nodes.push_back({v, geometry::NodeType::Corner, {}, {}});
    }
    return p;
}

geometry::PathSet rect_mm(double x0, double y0, double x1, double y1) {
    return geometry::PathSet{polygon({um(x0, y0), um(x1, y0), um(x1, y1), um(x0, y1)}), {}};
}

double dist_um(Vec2um a, Vec2um b) {
    return std::hypot(double(a.x.value) - double(b.x.value), double(a.y.value) - double(b.y.value));
}

// Plus grande distance au bord d'un carré [0 ; side] mm : 0 au bord, side/2 au centre.
double depth_mm(Vec2um p, double sideMm) {
    const double x = p.x.value / 1000.0;
    const double y = p.y.value / 1000.0;
    return std::min({x, y, sideMm - x, sideMm - y});
}

document::ContourFillParams params() {
    document::ContourFillParams p;
    p.row_spacing = Micrometers{400};
    p.stitch_length = Micrometers{3'000};
    p.min_length = Micrometers{500};
    p.inset = Micrometers{200};
    return p;
}

} // namespace

TEST_CASE("contour fill: concentric rings run from the edge to the centre without jumps") {
    const auto stitches = fill_contour(rect_mm(0, 0, 20, 20), params());
    REQUIRE(stitches.size() > 100);
    for (const FillStitch& s : stitches) {
        CHECK_FALSE(s.jump);
        CHECK(s.pos.x.value >= 0);
        CHECK(s.pos.x.value <= 20'000);
        CHECK(s.pos.y.value >= 0);
        CHECK(s.pos.y.value <= 20'000);
    }
    // Spirale : la profondeur croit globalement du bord (0) vers le centre (10 mm).
    CHECK(depth_mm(stitches.front().pos, 20.0) < 0.5);
    CHECK(depth_mm(stitches.back().pos, 20.0) > 8.0);
    // Chaque dixième de la couture est plus profond que le précédent (moyenne).
    double previous = -1.0;
    const std::size_t tenth = stitches.size() / 10;
    for (std::size_t k = 0; k < 10; ++k) {
        double sum = 0.0;
        for (std::size_t i = k * tenth; i < (k + 1) * tenth; ++i) {
            sum += depth_mm(stitches[i].pos, 20.0);
        }
        CHECK(sum / double(tenth) > previous);
        previous = sum / double(tenth);
    }
}

TEST_CASE("contour fill: stitch lengths stay within the maximum and the minimum") {
    auto p = params();
    p.stitch_length = Micrometers{2'500};
    p.min_length = Micrometers{1'200};
    const auto stitches = fill_contour(rect_mm(0, 0, 40, 40), p);
    REQUIRE(stitches.size() > 200);
    int tooLong = 0;
    int tooShort = 0;
    for (std::size_t i = 1; i < stitches.size(); ++i) {
        REQUIRE_FALSE(stitches[i].jump);
        const double d = dist_um(stitches[i - 1].pos, stitches[i].pos);
        tooLong += d > 2'500.0 + 1.0 ? 1 : 0;
        // Les petits anneaux du centre (profondeur > 17 mm) n'ont pas la place d'un point long.
        if (depth_mm(stitches[i].pos, 40.0) < 17.0) {
            tooShort += d < 1'200.0 * 0.9 ? 1 : 0;
        }
    }
    CHECK(tooLong == 0);
    CHECK(tooShort == 0);
}

TEST_CASE("contour fill: out of range values are clamped") {
    auto p = params();
    p.stitch_length = Micrometers{50'000}; // borné à 7 mm
    p.row_spacing = Micrometers{-5};       // borné à 0,1 mm
    p.min_length = Micrometers{90'000};    // borné au maximum
    const auto stitches = fill_contour(rect_mm(0, 0, 8, 8), p);
    REQUIRE_FALSE(stitches.empty());
    for (std::size_t i = 1; i < stitches.size(); ++i) {
        if (!stitches[i].jump) {
            CHECK(dist_um(stitches[i - 1].pos, stitches[i].pos) <= 7'000.0 + 1.0);
        }
    }
}

TEST_CASE("contour fill: holes are surrounded by growing rings and never crossed") {
    geometry::PathSet shape = rect_mm(0, 0, 30, 30);
    shape.holes.push_back(polygon({um(10, 10), um(10, 20), um(20, 20), um(20, 10)}));
    const auto stitches = fill_contour(shape, params());
    REQUIRE(stitches.size() > 300);

    const auto insideHole = [](double xMm, double yMm) {
        return xMm > 10.05 && xMm < 19.95 && yMm > 10.05 && yMm < 19.95;
    };
    double nearestToHole = 1e9;
    for (std::size_t i = 0; i < stitches.size(); ++i) {
        const double x = stitches[i].pos.x.value / 1000.0;
        const double y = stitches[i].pos.y.value / 1000.0;
        CHECK_FALSE(insideHole(x, y));
        // Distance au bord du trou (extérieur).
        const double dx = std::max({10.0 - x, 0.0, x - 20.0});
        const double dy = std::max({10.0 - y, 0.0, y - 20.0});
        nearestToHole = std::min(nearestToHole, std::hypot(dx, dy));
        if (i > 0 && !stitches[i].jump) {
            const double mx = (x + stitches[i - 1].pos.x.value / 1000.0) / 2.0;
            const double my = (y + stitches[i - 1].pos.y.value / 1000.0) / 2.0;
            CHECK_FALSE(insideHole(mx, my));
        }
    }
    // Des anneaux contournent le trou de près (retrait 0,2 mm, marge d'un pas).
    CHECK(nearestToHole < 0.6);
}

TEST_CASE("contour fill: non spiral rings are closed loops linked by a short step") {
    auto p = params();
    p.spiral = false;
    const auto stitches = fill_contour(rect_mm(0, 0, 20, 20), p);
    REQUIRE(stitches.size() > 100);
    int returns = 0; // anneau refermé : le point de départ de l'anneau est recousu
    for (std::size_t i = 2; i < stitches.size(); ++i) {
        CHECK_FALSE(stitches[i].jump);
        for (std::size_t j = 0; j + 1 < i && j < 3; ++j) {
            returns += stitches[i].pos == stitches[j].pos ? 1 : 0;
        }
    }
    CHECK(returns >= 1);
}

TEST_CASE("contour fill: degenerate region gives no stitches") {
    CHECK(fill_contour(geometry::PathSet{}, params()).empty());
}

TEST_CASE("contour fill: output is deterministic through the generation pipeline") {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    geometry::PathSet shape = rect_mm(0, 0, 25, 18);
    shape.holes.push_back(polygon({um(10, 6), um(10, 10), um(14, 10), um(14, 6)}));
    vec.paths.push_back(shape);
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = params();
    project.embroidery_objects.push_back(emb);

    const auto a = generate_sequence(project);
    const auto b = generate_sequence(project);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->commands.size() > 100);
    CHECK(a->commands == b->commands);
    const auto stitchCount =
        std::count_if(a->commands.begin(), a->commands.end(),
                      [](const auto& c) { return c.type == stitch::CommandType::Stitch; });
    CHECK(stitchCount > 100);
}
