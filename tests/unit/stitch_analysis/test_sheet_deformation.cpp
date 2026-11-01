// SPDX-License-Identifier: Apache-2.0
//
// Solveur mécanique 2D de la feuille tissu : symétries, linéarité, bord fixé, anisotropie,
// raidissement, déterminisme.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "openstitch/stitch_analysis/sheet_deformation.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;
using stitch::StitchPass;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

stitch::StitchSequence one_stitch(Vec2um a, Vec2um b) {
    stitch::StitchSequence s;
    s.commands.push_back({a, CommandType::Stitch, ObjectId{1}, StitchPass::TopStitch});
    s.commands.push_back({b, CommandType::Stitch, ObjectId{1}, StitchPass::TopStitch});
    return s;
}

// Domaine 60 x 60 mm centré, maillage 1 mm : le noeud (30, 30) est l'origine.
const SheetDomain kDomain{um(-30000, -30000), um(30000, 30000)};
constexpr Micrometers kCell{1000};

double ux(const SheetDeformation& d, std::size_t i, std::size_t j) {
    return d.ux_mm[j * d.node_columns() + i];
}
double uy(const SheetDeformation& d, std::size_t i, std::size_t j) {
    return d.uy_mm[j * d.node_columns() + i];
}

SheetParams tight() {
    SheetParams p;
    p.tolerance = 1e-11;
    return p;
}

} // namespace

TEST_CASE("sheet deformation: no stitch gives an empty result", "[sheet][deformation]") {
    const auto d = solve_sheet_deformation({}, tight(), kDomain, kCell);
    CHECK(d.empty());
    CHECK(d.segment_count == 0);
}

TEST_CASE("sheet deformation: a stitch pulls its ends toward each other", "[sheet][deformation]") {
    const auto seq = one_stitch(um(-3000, 0), um(3000, 0));
    const auto d = solve_sheet_deformation(seq, tight(), kDomain, kCell);
    REQUIRE_FALSE(d.empty());
    CHECK(d.converged);
    CHECK(d.columns == 60);
    CHECK(d.rows == 60);
    // Extrémités en x = -3 et +3 -> noeuds (27, 30) et (33, 30).
    CHECK(ux(d, 27, 30) > 0.0);
    CHECK(ux(d, 33, 30) < 0.0);
    // Antisymétrie en x, symétrie des amplitudes.
    CHECK(ux(d, 27, 30) == Catch::Approx(-ux(d, 33, 30)).epsilon(1e-6));
    // Antisymétrie de uy par rapport à l'axe du point.
    CHECK(uy(d, 30, 35) == Catch::Approx(-uy(d, 30, 25)).margin(1e-9));
    CHECK(d.max_displacement_mm > 0.0);
}

TEST_CASE("sheet deformation: the hoop edge stays fixed", "[sheet][deformation]") {
    const auto seq = one_stitch(um(-3000, 0), um(3000, 0));
    const auto d = solve_sheet_deformation(seq, tight(), kDomain, kCell);
    REQUIRE_FALSE(d.empty());
    for (std::size_t k = 0; k <= d.columns; ++k) {
        CHECK(ux(d, k, 0) == 0.0);
        CHECK(uy(d, k, d.rows) == 0.0);
    }
    for (std::size_t k = 0; k <= d.rows; ++k) {
        CHECK(ux(d, 0, k) == 0.0);
        CHECK(uy(d, d.columns, k) == 0.0);
    }
}

TEST_CASE("sheet deformation: response is linear in tension and inverse in stiffness",
          "[sheet][deformation]") {
    const auto seq = one_stitch(um(-3000, 0), um(3000, 0));
    const SheetParams base = tight();
    const auto d1 = solve_sheet_deformation(seq, base, kDomain, kCell);

    SheetParams strong = base;
    strong.thread_tension_n *= 2.0;
    const auto d2 = solve_sheet_deformation(seq, strong, kDomain, kCell);
    CHECK(d2.max_displacement_mm == Catch::Approx(2.0 * d1.max_displacement_mm).epsilon(1e-6));

    SheetParams stiff = base;
    stiff.stiffness_warp_n_per_mm *= 2.0;
    stiff.stiffness_weft_n_per_mm *= 2.0;
    const auto d3 = solve_sheet_deformation(seq, stiff, kDomain, kCell);
    CHECK(d3.max_displacement_mm == Catch::Approx(0.5 * d1.max_displacement_mm).epsilon(1e-6));
}

TEST_CASE("sheet deformation: a softer weft yields more displacement across the weft",
          "[sheet][deformation]") {
    SheetParams p = tight();
    p.stiffness_warp_n_per_mm = 80.0;
    p.stiffness_weft_n_per_mm = 20.0;
    // Chaîne selon X : le point horizontal tire en chaîne, le vertical en trame.
    const auto along_warp =
        solve_sheet_deformation(one_stitch(um(-3000, 0), um(3000, 0)), p, kDomain, kCell);
    const auto along_weft =
        solve_sheet_deformation(one_stitch(um(0, -3000), um(0, 3000)), p, kDomain, kCell);
    CHECK(along_weft.max_displacement_mm > 1.2 * along_warp.max_displacement_mm);

    // Tourner le sens du tissu de 90 degres inverse le rapport.
    p.grain_angle_rad = std::acos(-1.0) / 2.0;
    const auto rotated =
        solve_sheet_deformation(one_stitch(um(-3000, 0), um(3000, 0)), p, kDomain, kCell);
    CHECK(rotated.max_displacement_mm ==
          Catch::Approx(along_weft.max_displacement_mm).epsilon(1e-6));
}

TEST_CASE("sheet deformation: compression flags a wrinkle index at the stitch",
          "[sheet][deformation]") {
    const auto seq = one_stitch(um(-3000, 0), um(3000, 0));
    SheetParams p = tight();
    const auto d = solve_sheet_deformation(seq, p, kDomain, kCell);
    CHECK(d.max_wrinkle_index > 0.0);
    CHECK(std::abs(d.max_wrinkle_at.x.value) < 8000);
    CHECK(std::abs(d.max_wrinkle_at.y.value) < 8000);
    CHECK(d.p99_wrinkle_index <= d.max_wrinkle_index + 1e-6);

    SheetParams fragile = p;
    fragile.critical_compressive_strain = p.critical_compressive_strain / 2.0;
    const auto d2 = solve_sheet_deformation(seq, fragile, kDomain, kCell);
    CHECK(d2.max_wrinkle_index == Catch::Approx(2.0 * d.max_wrinkle_index).epsilon(1e-5));
}

TEST_CASE("sheet deformation: thread stiffens the sheet", "[sheet][deformation]") {
    const auto seq = one_stitch(um(-3000, 0), um(3000, 0));
    SheetParams loose = tight();
    loose.stitch_stiffening = 0.0;
    SheetParams stiffened = loose;
    stiffened.stitch_stiffening = 1.0;
    const auto a = solve_sheet_deformation(seq, loose, kDomain, kCell);
    const auto b = solve_sheet_deformation(seq, stiffened, kDomain, kCell);
    CHECK(b.max_displacement_mm < a.max_displacement_mm);
}

TEST_CASE("sheet deformation: same input gives the same output", "[sheet][deformation]") {
    const auto seq = one_stitch(um(-4000, -1000), um(5000, 2000));
    const auto a = solve_sheet_deformation(seq, SheetParams{}, kDomain, kCell);
    const auto b = solve_sheet_deformation(seq, SheetParams{}, kDomain, kCell);
    CHECK(a.ux_mm == b.ux_mm);
    CHECK(a.uy_mm == b.uy_mm);
    CHECK(a.wrinkle_index == b.wrinkle_index);
    CHECK(a.iterations == b.iterations);
}

TEST_CASE("sheet deformation: grid is capped and domain helper wraps the stitches",
          "[sheet][deformation]") {
    const auto seq = one_stitch(um(-3000, 0), um(3000, 0));
    const auto dom = domain_around_stitches(seq, Micrometers{10000});
    CHECK(dom.min == um(-13000, -10000));
    CHECK(dom.max == um(13000, 10000));
    const auto d = solve_sheet_deformation(seq, SheetParams{}, kDomain, Micrometers{100}, 40);
    CHECK(d.columns <= 40);
    CHECK(d.rows <= 40);
    CHECK(domain_around_stitches({}, Micrometers{1000}).max == um(0, 0));
}

TEST_CASE("sheet deformation: stabilizer stiffens the preset and raster has a pixel per element",
          "[sheet][deformation]") {
    const SheetParams none = sheet_preset("woven", "none");
    const SheetParams cut = sheet_preset("woven", "cutaway");
    CHECK(cut.stiffness_warp_n_per_mm > none.stiffness_warp_n_per_mm);
    CHECK(sheet_preset("inconnu", "").stiffness_warp_n_per_mm == none.stiffness_warp_n_per_mm);

    const auto d =
        solve_sheet_deformation(one_stitch(um(-3000, 0), um(3000, 0)), none, kDomain, kCell);
    const SheetRaster r = rasterize_wrinkle_index(d);
    CHECK(r.width == 60);
    CHECK(r.height == 60);
    CHECK(r.rgba.size() == 60U * 60U * 4U);
    bool any = false;
    for (std::size_t i = 3; i < r.rgba.size(); i += 4) {
        any = any || r.rgba[i] != 0;
    }
    CHECK(any);
}
