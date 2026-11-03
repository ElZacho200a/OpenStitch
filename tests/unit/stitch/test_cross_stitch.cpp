// SPDX-License-Identifier: Apache-2.0
// Point de croix (HP-STI-012).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "openstitch/stitch_generation/cross_stitch.hpp"
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

geometry::PathSet rect(double w, double h) {
    return geometry::PathSet{polygon({um(0, 0), um(w, 0), um(w, h), um(0, h)}), {}};
}

geometry::PathSet square_with_hole() {
    geometry::PathSet set = rect(30, 30);
    set.holes.push_back(polygon({um(10, 10), um(10, 20), um(20, 20), um(20, 10)}));
    return set;
}

// Nombre de jambes : segments cousus dont le deplacement est une diagonale d'une cellule.
int count_legs(const std::vector<FillStitch>& s, int cell) {
    int legs = 0;
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (s[i].jump) {
            continue;
        }
        const int dx = std::abs(s[i].pos.x.value - s[i - 1].pos.x.value);
        const int dy = std::abs(s[i].pos.y.value - s[i - 1].pos.y.value);
        if (dx == cell && dy == cell) {
            ++legs;
        }
    }
    return legs;
}

} // namespace

TEST_CASE("cross stitch: a 10 mm square with 2.5 mm cells holds 16 full crosses") {
    document::CrossStitchParams p;
    const auto s = fill_cross_stitch(rect(10, 10), p);
    REQUIRE(!s.empty());
    CHECK(count_legs(s, 2'500) == 32); // 16 cellules x 2 jambes
    for (const FillStitch& f : s) {
        CHECK(f.pos.x.value >= 0);
        CHECK(f.pos.y.value >= 0);
        CHECK(f.pos.x.value <= 10'000);
        CHECK(f.pos.y.value <= 10'000);
        CHECK(f.pos.x.value % 2'500 == 0); // tous les points sont des coins de cellule
        CHECK(f.pos.y.value % 2'500 == 0);
    }
}

TEST_CASE("cross stitch: half cross sews one leg per cell") {
    document::CrossStitchParams p;
    p.full_cross = false;
    const auto s = fill_cross_stitch(rect(10, 10), p);
    CHECK(count_legs(s, 2'500) == 16);
}

TEST_CASE("cross stitch: a row is sewn forward with slash legs then back with backslash legs") {
    document::CrossStitchParams p;
    const auto s = fill_cross_stitch(rect(7.5, 2.5), p);
    REQUIRE(s.size() == 12);
    // Aller : trois « / » (vers le haut a droite), retour : trois « \ » (vers le haut a gauche).
    for (std::size_t i = 0; i < 6; i += 2) {
        CHECK(s[i + 1].pos.x.value - s[i].pos.x.value == 2'500);
        CHECK(s[i + 1].pos.y.value - s[i].pos.y.value == 2'500);
    }
    for (std::size_t i = 6; i < 12; i += 2) {
        CHECK(s[i + 1].pos.x.value - s[i].pos.x.value == -2'500);
        CHECK(s[i + 1].pos.y.value - s[i].pos.y.value == 2'500);
    }
    // La rangee suivante demarre ou la precedente finit : pas de saut entre rangees.
    const auto two = fill_cross_stitch(rect(7.5, 5.0), p);
    CHECK(std::none_of(two.begin() + 1, two.end(), [](const FillStitch& f) { return f.jump; }));
}

TEST_CASE("cross stitch: cells whose centre is in a hole are skipped") {
    document::CrossStitchParams p;
    const auto s = fill_cross_stitch(square_with_hole(), p);
    CHECK(count_legs(s, 2'500) == 2 * (144 - 16));
    for (const FillStitch& f : s) {
        const bool inHole = f.pos.x.value > 10'050 && f.pos.x.value < 19'950 &&
                            f.pos.y.value > 10'050 && f.pos.y.value < 19'950;
        CHECK_FALSE(inHole);
    }
    CHECK(std::any_of(s.begin() + 1, s.end(), [](const FillStitch& f) { return f.jump; }));
}

TEST_CASE("cross stitch: rotated grid stays within the region plus a cell margin") {
    document::CrossStitchParams p;
    p.grid_angle = Angle{0.5};
    const auto s = fill_cross_stitch(rect(20, 20), p);
    REQUIRE(s.size() > 100);
    const int margin = 2'500 * 3 / 2;
    for (const FillStitch& f : s) {
        CHECK(f.pos.x.value >= -margin);
        CHECK(f.pos.y.value >= -margin);
        CHECK(f.pos.x.value <= 20'000 + margin);
        CHECK(f.pos.y.value <= 20'000 + margin);
    }
}

TEST_CASE("cross stitch: cell size and passes are clamped") {
    document::CrossStitchParams p;
    p.cell = Micrometers{100}; // borne a 1 mm
    CHECK(count_legs(fill_cross_stitch(rect(10, 10), p), 1'000) == 200);
    p.cell = Micrometers{2'500};
    p.passes = 2;
    CHECK(count_legs(fill_cross_stitch(rect(10, 10), p), 2'500) == 64);
    p.passes = 99; // borne a 3
    CHECK(count_legs(fill_cross_stitch(rect(10, 10), p), 2'500) == 96);
}

TEST_CASE("cross stitch: output is deterministic through the generation pipeline") {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(square_with_hole());
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    document::CrossStitchParams params;
    params.grid_angle = Angle{0.3};
    emb.params = params;
    CHECK(emb.is_cross_stitch());
    project.embroidery_objects.push_back(emb);

    const auto a = generate_sequence(project);
    const auto b = generate_sequence(project);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->commands.size() > 200);
    CHECK(a->commands == b->commands);
}
