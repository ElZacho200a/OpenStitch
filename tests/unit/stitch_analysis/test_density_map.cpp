// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_analysis/density_map.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;
using stitch::StitchPass;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

void stitch_at(stitch::StitchSequence& s, std::int32_t x, std::int32_t y, std::uint64_t source) {
    s.commands.push_back({um(x, y), CommandType::Stitch, ObjectId{source}, StitchPass::TopStitch});
}

// `perCell` piqûres dans chacune des cases de 1 mm d'un bloc columns x rows, dont le coin
// bas-gauche de la première case est (x0, y0) ; positions distinctes à l'intérieur de la case.
void dense_block(stitch::StitchSequence& s, std::int32_t x0, std::int32_t y0, int columns, int rows,
                 int perCell, std::uint64_t source) {
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            for (int k = 0; k < perCell; ++k) {
                stitch_at(s, x0 + c * 1000 + 100 + k * 100, y0 + r * 1000 + 100 + k * 100, source);
            }
            // Un saut entre cases : pas de segment de fil d'une case à l'autre.
            s.commands.push_back({um(x0, y0), CommandType::Jump, ObjectId{source}});
        }
    }
}

} // namespace

TEST_CASE("density map of an empty sequence is empty") {
    const auto map = compute_density_map(stitch::StitchSequence{});
    CHECK(map.empty());
    CHECK(map.columns == 0);
    CHECK(map.rows == 0);
    CHECK(find_density_hotspots(stitch::StitchSequence{}).empty());
}

TEST_CASE("density map counts penetrations per cell on a grid aligned to the cell size") {
    stitch::StitchSequence s;
    // Négatif : l'origine de la grille est un multiple du côté de case, pas le premier point.
    stitch_at(s, -1'500, -500, 1);
    stitch_at(s, -1'400, -400, 1); // même case que la précédente
    stitch_at(s, 1'200, 700, 1);
    const auto map = compute_density_map(s);
    REQUIRE_FALSE(map.empty());
    CHECK(map.origin.x.value == -2'000);
    CHECK(map.origin.y.value == -1'000);
    CHECK(map.columns == 4); // x de -2 mm à 2 mm
    CHECK(map.rows == 2);    // y de -1 mm à 1 mm
    CHECK(map.penetrations[0] == 2);
    CHECK(map.density_per_mm2(0, 0) == 2.0);
    CHECK(map.density_per_mm2(3, 1) == 1.0);
    CHECK(map.density_per_mm2(1, 0) == 0.0);
    CHECK(map.density_per_mm2(99, 99) == 0.0);
    CHECK(map.max_density_per_mm2() == 2.0);
}

TEST_CASE("density map thread thickness follows sewn length") {
    stitch::StitchSequence s;
    // Rangée dense : 10 segments de 1 mm en 1 mm de large, espacés de 0,1 mm (case de 1 mm).
    for (int i = 0; i < 10; ++i) {
        stitch_at(s, 50, 50 + i * 100, 1);
        stitch_at(s, 950, 50 + i * 100, 1);
        s.commands.push_back({um(950, 50 + i * 100), CommandType::Jump, ObjectId{1}});
    }
    const auto map = compute_density_map(s);
    REQUIRE(map.columns == 1);
    REQUIRE(map.rows == 1);
    // 10 x 0,9 mm de fil de 0,4 mm de large sur 1 mm2 : 3,6 couches.
    CHECK(map.thickness_layers(0, 0, Micrometers{400}) == Catch::Approx(3.6).margin(1e-9));
    CHECK(map.penetrations[0] == 20);
}

TEST_CASE("density hotspots group contiguous over-dense cells and name the main object") {
    stitch::StitchSequence s;
    dense_block(s, 0, 0, 3, 2, 6, 7);      // 6 /mm2 sur 3 x 2 cases : sur-dense
    dense_block(s, 10'000, 0, 1, 1, 6, 9); // une autre case isolée sur-dense, plus loin
    dense_block(s, 0, 5'000, 4, 1, 2, 7);  // 2 /mm2 : sous le seuil
    const auto hot = find_density_hotspots(s);
    REQUIRE(hot.size() == 2);
    CHECK(hot[0].cells == 6);
    CHECK(hot[0].area_mm2 == 6.0);
    CHECK(hot[0].max_penetrations_per_mm2 == 6.0);
    CHECK(hot[0].object == ObjectId{7});
    CHECK(hot[0].box.min == um(0, 0));
    CHECK(hot[0].box.max == um(3'000, 2'000));
    CHECK(hot[0].center == um(500, 500));
    CHECK(hot[1].cells == 1);
    CHECK(hot[1].object == ObjectId{9});
}

TEST_CASE("density hotspots respect the configurable threshold") {
    stitch::StitchSequence s;
    dense_block(s, 0, 0, 2, 2, 6, 1);
    DensityOptions strict;
    strict.max_penetrations_per_mm2 = 5.0;
    CHECK(find_density_hotspots(s, strict).size() == 1);
    DensityOptions lax;
    lax.max_penetrations_per_mm2 = 6.0; // strictement supérieur : 6 ne dépasse pas
    CHECK(find_density_hotspots(s, lax).empty());
    DensityOptions off;
    off.max_penetrations_per_mm2 = 0.0;
    CHECK(find_density_hotspots(s, off).empty());
}

TEST_CASE("density map doubles the cell size to respect the cell cap") {
    stitch::StitchSequence s;
    stitch_at(s, 0, 0, 1);
    stitch_at(s, 99'000, 99'000, 1); // 100 x 100 cases de 1 mm
    DensityOptions options;
    options.max_cells = 2'500;
    const auto map = compute_density_map(s, options);
    CHECK(map.cell.value == 2'000);
    CHECK(map.columns * map.rows <= 2'500);
    std::uint32_t total = 0;
    for (const auto n : map.penetrations) {
        total += n;
    }
    CHECK(total == 2);
}

TEST_CASE("density map is deterministic") {
    stitch::StitchSequence s;
    dense_block(s, -3'000, 2'000, 3, 3, 5, 4);
    const auto a = compute_density_map(s);
    const auto b = compute_density_map(s);
    CHECK(a.penetrations == b.penetrations);
    CHECK(a.thread_um == b.thread_um);
    const auto ha = find_density_hotspots(s);
    const auto hb = find_density_hotspots(s);
    REQUIRE(ha.size() == hb.size());
    for (std::size_t i = 0; i < ha.size(); ++i) {
        CHECK(ha[i].center == hb[i].center);
        CHECK(ha[i].cells == hb[i].cells);
    }
}
