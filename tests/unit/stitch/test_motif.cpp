// SPDX-License-Identifier: Apache-2.0
// Motif le long d'un chemin (HP-STI-002) et remplissage a motif (HP-STI-006).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "openstitch/stitch_generation/motif_fill.hpp"
#include "openstitch/stitch_generation/motif_run.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

Vec2um um(double xMm, double yMm) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(xMm * 1000.0))},
                  Micrometers{static_cast<std::int32_t>(std::lround(yMm * 1000.0))}};
}

geometry::Path polygon(const std::vector<Vec2um>& pts, bool closed) {
    geometry::Path p;
    p.closed = closed;
    for (const Vec2um v : pts) {
        p.nodes.push_back({v, geometry::NodeType::Corner, {}, {}});
    }
    return p;
}

geometry::PathSet rect_mm(double x0, double y0, double x1, double y1) {
    return geometry::PathSet{polygon({um(x0, y0), um(x1, y0), um(x1, y1), um(x0, y1)}, true), {}};
}

double dist_um(Vec2um a, Vec2um b) {
    return std::hypot(double(a.x.value) - double(b.x.value), double(a.y.value) - double(b.y.value));
}

} // namespace

TEST_CASE("motif library: every pattern starts at 0,0 and ends at 1000,0") {
    for (int i = 0; i <= static_cast<int>(document::MotifId::Scallop); ++i) {
        const auto pattern = motif_pattern(static_cast<document::MotifId>(i));
        REQUIRE(pattern.size() >= 4);
        CHECK(pattern.front().x == 0);
        CHECK(pattern.front().y == 0);
        CHECK(pattern.back().x == 1000);
        CHECK(pattern.back().y == 0);
        for (const MotifPoint& p : pattern) {
            CHECK(p.x >= 0);
            CHECK(p.x <= 1000);
            CHECK(std::abs(p.y) <= 500);
        }
    }
}

TEST_CASE("motif run: repeats along a straight path within the size band") {
    document::MotifRunParams params;
    params.motif = document::MotifId::Wave;
    params.size = Micrometers{3'000};
    params.spacing = Micrometers{5'000};
    const auto pts = motif_run_points(polygon({um(0, 0), um(30, 0)}, false), params);
    REQUIRE(pts.size() > 12);
    CHECK(pts.front() == um(0, 0));
    CHECK(pts.back() == um(30, 0));
    for (const Vec2um p : pts) {
        CHECK(std::abs(p.y.value) <= 1'500);
        CHECK(p.x.value >= 0);
        CHECK(p.x.value <= 30'000);
    }
    // La vague s'etend des deux cotes de l'axe.
    CHECK(std::any_of(pts.begin(), pts.end(), [](Vec2um p) { return p.y.value > 1'000; }));
    CHECK(std::any_of(pts.begin(), pts.end(), [](Vec2um p) { return p.y.value < -1'000; }));
}

TEST_CASE("motif run: stitch lengths stay within the configured bounds") {
    document::MotifRunParams params;
    params.motif = document::MotifId::Heart;
    params.size = Micrometers{6'000};
    params.spacing = Micrometers{8'000};
    params.stitch_length = Micrometers{2'500};
    params.min_length = Micrometers{500};
    const auto pts = motif_run_points(polygon({um(0, 0), um(40, 0)}, false), params);
    REQUIRE(pts.size() > 10);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const double d = dist_um(pts[i - 1], pts[i]);
        CHECK(d <= 2'500.0 + 2.0);
        if (i + 1 < pts.size()) {
            CHECK(d >= 499.0);
        }
    }
}

TEST_CASE("motif run: alternate mirrors every second repetition") {
    document::MotifRunParams params;
    params.motif = document::MotifId::Scallop;
    params.size = Micrometers{4'000};
    params.spacing = Micrometers{10'000};
    const geometry::Path line = polygon({um(0, 0), um(20, 0)}, false);
    const auto plain = motif_run_points(line, params);
    params.alternate = true;
    const auto alt = motif_run_points(line, params);
    CHECK(std::all_of(plain.begin(), plain.end(), [](Vec2um p) { return p.y.value >= 0; }));
    CHECK(std::any_of(alt.begin(), alt.end(), [](Vec2um p) { return p.y.value < -1'000; }));
}

TEST_CASE("motif run: a closed path closes on its start and output is deterministic") {
    document::MotifRunParams params;
    const geometry::Path square = polygon({um(0, 0), um(20, 0), um(20, 20), um(0, 20)}, true);
    const auto a = motif_run_points(square, params);
    const auto b = motif_run_points(square, params);
    REQUIRE(!a.empty());
    CHECK(a == b);
    CHECK(a.front() == a.back());
    CHECK(motif_run_points(polygon({um(1, 1)}, false), params).empty());
}

TEST_CASE("motif fill: strokes stay inside the region and avoid holes") {
    document::MotifFillParams params;
    params.motif = document::MotifId::Diamond;
    params.size = Micrometers{3'000};
    params.spacing = Micrometers{4'000};
    geometry::PathSet region = rect_mm(0, 0, 30, 30);
    region.holes.push_back(polygon({um(10, 10), um(20, 10), um(20, 20), um(10, 20)}, true));
    const auto fill = fill_motif(region, params);
    REQUIRE(fill.size() > 50);
    CHECK(fill.front().jump);
    for (const FillStitch& s : fill) {
        CHECK(s.pos.x.value >= 0);
        CHECK(s.pos.x.value <= 30'000);
        CHECK(s.pos.y.value >= 0);
        CHECK(s.pos.y.value <= 30'000);
        const bool inHole = s.pos.x.value > 10'001 && s.pos.x.value < 19'999 &&
                            s.pos.y.value > 10'001 && s.pos.y.value < 19'999;
        CHECK(!inHole);
    }
    for (std::size_t i = 1; i < fill.size(); ++i) {
        if (!fill[i].jump) {
            CHECK(dist_um(fill[i - 1].pos, fill[i].pos) <= 2'000.0 + 2.0);
        }
    }
}

TEST_CASE("motif fill: deterministic, angle rotates the grid, tiny region yields no crash") {
    document::MotifFillParams params;
    const geometry::PathSet region = rect_mm(0, 0, 25, 25);
    const auto a = fill_motif(region, params);
    CHECK(a == fill_motif(region, params));
    params.angle = Angle{0.5};
    const auto rotated = fill_motif(region, params);
    CHECK(!rotated.empty());
    CHECK(rotated != a);
    CHECK(fill_motif(rect_mm(0, 0, 0.2, 0.2), params).size() <= 4);
}
