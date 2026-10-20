// SPDX-License-Identifier: Apache-2.0
//
// HP-VIEW-014 : carte d'épaisseur estimée (conversion en mm, pic, résolution adaptée au zoom).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "openstitch/stitch_analysis/thickness_map.hpp"

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

// Rangées de fil de 1 mm de côté (pas 0,4 mm) : `rows` allers-retours dans la case (0..1 mm).
void fill_rows(stitch::StitchSequence& s, int rows, std::uint64_t source) {
    s.commands.push_back({um(0, 0), CommandType::Jump, ObjectId{source}});
    for (int r = 0; r < rows; ++r) {
        const int y = 50 + r * 100;
        stitch_at(s, r % 2 == 0 ? 50 : 950, y, source);
        stitch_at(s, r % 2 == 0 ? 950 : 50, y, source);
    }
}

} // namespace

TEST_CASE("thickness : une couche vaut layer_thickness, l'empilement est lineaire") {
    ThicknessOptions options;
    CHECK(layers_to_mm(1.0, options) == Catch::Approx(0.2));
    CHECK(layers_to_mm(3.0, options) == Catch::Approx(0.6));
    options.layer_thickness = Micrometers{150};
    CHECK(layers_to_mm(2.0, options) == Catch::Approx(0.3));
    CHECK(layers_to_mm(0.0, options) == 0.0);
    CHECK(layers_to_mm(-1.0, options) == 0.0);
}

TEST_CASE("thickness : une sequence vide n'a ni grille ni pic") {
    const stitch::StitchSequence empty;
    const auto grid = compute_thickness_grid(empty);
    CHECK(grid.empty());
    CHECK_FALSE(find_thickness_peak(empty, grid).found);
}

TEST_CASE("thickness : le pic est la case la plus epaisse, avec son objet et son depassement") {
    stitch::StitchSequence s;
    // Objet 1 : 3 rangees dans la case (0, 0) ; objet 2 : beaucoup plus dense dans la case (2, 0).
    fill_rows(s, 3, 1);
    s.commands.push_back({um(2'000, 0), CommandType::Jump, ObjectId{2}});
    for (int r = 0; r < 9; ++r) {
        const int y = 50 + r * 100;
        stitch_at(s, r % 2 == 0 ? 2'050 : 2'950, y, 2);
        stitch_at(s, r % 2 == 0 ? 2'950 : 2'050, y, 2);
    }
    ThicknessOptions options;
    const auto grid = compute_thickness_grid(s, options);
    REQUIRE_FALSE(grid.empty());
    const auto peak = find_thickness_peak(s, grid, options);
    REQUIRE(peak.found);
    CHECK(peak.object == ObjectId{2});
    CHECK(peak.position.x.value == 2'500); // centre de la case 2 mm .. 3 mm
    CHECK(peak.position.y.value == 500);
    CHECK(peak.layers > 2.0);
    CHECK(peak.mm == Catch::Approx(layers_to_mm(peak.layers, options)));
    CHECK(peak.over_limit == (peak.layers > options.max_layers));

    options.max_layers = 0.0; // règle désactivée : jamais de dépassement
    CHECK_FALSE(find_thickness_peak(s, grid, options).over_limit);
}

TEST_CASE("thickness : deux calculs identiques donnent la meme carte et le meme pic") {
    stitch::StitchSequence s;
    fill_rows(s, 5, 7);
    const auto a = compute_thickness_grid(s);
    const auto b = compute_thickness_grid(s);
    CHECK(a.penetrations == b.penetrations);
    CHECK(a.thread_um == b.thread_um);
    const auto pa = find_thickness_peak(s, a);
    const auto pb = find_thickness_peak(s, b);
    CHECK(pa.layers == pb.layers);
    CHECK(pa.position == pb.position);
}

TEST_CASE("thickness : la resolution suit le zoom, plus fine en zoom avant") {
    // 4 px par case : à 40 px/mm une case de 0,5 mm suffit ; à 2 px/mm il faut 2 mm.
    CHECK(adaptive_cell(40.0).value == 500);
    CHECK(adaptive_cell(8.0).value == 500);
    CHECK(adaptive_cell(4.0).value == 1'000);
    CHECK(adaptive_cell(2.0).value == 2'000);
    CHECK(adaptive_cell(1.0).value == 4'000);
    CHECK(adaptive_cell(0.5).value == 8'000);
    CHECK(adaptive_cell(0.01).value == 8'000); // borne haute
    // Monotone : un zoom plus petit ne donne jamais une case plus fine.
    std::int32_t previous = 0;
    for (const double zoom : {50.0, 20.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.1}) {
        const auto cell = adaptive_cell(zoom).value;
        CHECK(cell >= previous);
        previous = cell;
    }
    // Entrées invalides : valeur par défaut.
    CHECK(adaptive_cell(0.0).value == 1'000);
    CHECK(adaptive_cell(-3.0).value == 1'000);
    CHECK(adaptive_cell(std::numeric_limits<double>::quiet_NaN()).value == 1'000);
}

TEST_CASE("thickness : la case demandee est respectee par la grille") {
    stitch::StitchSequence s;
    fill_rows(s, 4, 1);
    ThicknessOptions options;
    options.cell = Micrometers{500};
    CHECK(compute_thickness_grid(s, options).cell.value == 500);
    options.cell = Micrometers{2'000};
    CHECK(compute_thickness_grid(s, options).cell.value == 2'000);
}
