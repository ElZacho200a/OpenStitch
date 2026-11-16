// SPDX-License-Identifier: Apache-2.0
// Fondu à la frontière tatami–tatami (lot L1b) : docs/studio-fondu-frontiere-spec.md.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/stitch_generation/boundary_blend.hpp"
#include "openstitch/stitch_generation/row_mask.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

geometry::PathSet rect(std::int32_t x0, std::int32_t y0, std::int32_t w, std::int32_t h) {
    const auto node = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{
            Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner, {}, {}};
    };
    geometry::PathSet set;
    set.outer.closed = true;
    set.outer.nodes = {node(x0, y0), node(x0 + w, y0), node(x0 + w, y0 + h), node(x0, y0 + h)};
    return set;
}

Vec2um at(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

document::TatamiParams params(double angle = 0.0) {
    document::TatamiParams p;
    p.angle = Angle{angle};
    p.row_spacing = Micrometers{400};
    p.stitch_length = Micrometers{3'000};
    p.inset = Micrometers{0};
    return p;
}

// Longueur cousue (ni saut ni trajet caché) dont le milieu tombe dans [lo ; hi) en x.
double sewn_in_band(const std::vector<FillStitch>& f, double lo, double hi,
                    bool horizontalOnly = true) {
    double sum = 0.0;
    for (std::size_t i = 1; i < f.size(); ++i) {
        const FillStitch& a = f[i - 1];
        const FillStitch& b = f[i];
        if (b.jump || b.travel || a.travel) {
            continue;
        }
        if (horizontalOnly && a.pos.y.value != b.pos.y.value) {
            continue;
        }
        const double ax = a.pos.x.value;
        const double bx = b.pos.x.value;
        if (horizontalOnly) {
            sum += std::max(0.0, std::min(std::max(ax, bx), hi) - std::max(std::min(ax, bx), lo));
            continue;
        }
        const double mid = 0.5 * (ax + bx);
        if (mid >= lo && mid < hi) {
            sum += std::hypot(bx - ax, static_cast<double>(b.pos.y.value - a.pos.y.value));
        }
    }
    return sum;
}

} // namespace

TEST_CASE("boundary blend: density ramps across the shared edge", "[boundary_blend]") {
    const auto a = rect(0, 0, 10'000, 10'000);
    const auto b = rect(10'000, 0, 10'000, 10'000);
    const auto plan = plan_boundary_blend(a, b, BoundaryBlendSpec{});
    REQUIRE(plan.has_value());
    const auto& field = *plan->field;
    CHECK(field.density_a(at(2'000, 5'000)) == 1.0);  // cœur de A
    CHECK(field.density_a(at(18'000, 5'000)) == 0.0); // cœur de B
    CHECK(field.density_a(at(7'000, 5'000)) == Catch::Approx(1.0).margin(1e-9));
    CHECK(field.density_a(at(9'990, 5'000)) == Catch::Approx(0.5).margin(0.01));
    CHECK(field.density_a(at(11'250, 5'000)) == Catch::Approx(0.25).margin(0.01));
    CHECK(field.density_a(at(13'000, 5'000)) == Catch::Approx(0.0).margin(1e-9));
    CHECK(field.signed_distance(at(8'000, 5'000)) < 0.0);
    CHECK(field.signed_distance(at(12'000, 5'000)) > 0.0);
}

TEST_CASE("boundary blend: each zone reaches into the other half band only", "[boundary_blend]") {
    const auto a = rect(0, 0, 10'000, 10'000);
    const auto b = rect(10'000, 0, 10'000, 10'000);
    BoundaryBlendSpec spec;
    spec.width_a = Micrometers{2'000};
    spec.width_b = Micrometers{3'000};
    const auto plan = plan_boundary_blend(a, b, spec);
    REQUIRE(plan.has_value());
    double areaA = 0.0;
    for (const auto& s : plan->region_a) {
        areaA += geometry::path_set_area_um2(s);
    }
    double areaB = 0.0;
    for (const auto& s : plan->region_b) {
        areaB += geometry::path_set_area_um2(s);
    }
    // A (100 mm²) + 3 mm × 10 mm dans B ; B (100 mm²) + 2 mm × 10 mm dans A.
    CHECK(areaA == Catch::Approx(130e6).epsilon(0.01));
    CHECK(areaB == Catch::Approx(120e6).epsilon(0.01));

    const auto fills = fill_boundary_blend_tatami(a, params(), b, params(), spec);
    REQUIRE(fills.has_value());
    REQUIRE_FALSE(fills->a.empty());
    REQUIRE_FALSE(fills->b.empty());
    // Une zone ne coud au-delà de la demi-bande de l'autre qu'une demi-case de pénétrations au
    // plus (décision par case de 3 mm, densité presque nulle au bord de bande).
    for (const FillStitch& s : fills->a) {
        CHECK(s.pos.x.value >= -2);
        CHECK(s.pos.x.value <= 13'000 + 1'500 + 2);
    }
    for (const FillStitch& s : fills->b) {
        CHECK(s.pos.x.value >= 8'000 - 1'500 - 2);
        CHECK(s.pos.x.value <= 20'000 + 2);
    }
}

TEST_CASE("boundary blend: same grid shares every row segment exactly once", "[boundary_blend]") {
    const auto a = rect(0, 0, 10'000, 10'000);
    const auto b = rect(10'000, 0, 10'000, 10'000);
    const auto fills = fill_boundary_blend_tatami(a, params(), b, params(), BoundaryBlendSpec{});
    REQUIRE(fills.has_value());
    // Référence : tatami plein des deux zones réunies, grille ancrée (masque plein).
    const DensityRowMask full([](Vec2um) { return 1.0; }, DensityRowMask::Slice::Lower);
    const auto plain = fill_tatami(rect(0, 0, 20'000, 10'000), params(), &full);
    for (double lo = 5'000.0; lo < 15'000.0; lo += 1'000.0) {
        const double fa = sewn_in_band(fills->a, lo, lo + 1'000.0);
        const double fb = sewn_in_band(fills->b, lo, lo + 1'000.0);
        const double ff = sewn_in_band(plain, lo, lo + 1'000.0);
        INFO("band " << lo << " : A " << fa << " B " << fb << " plain " << ff);
        REQUIRE(ff > 0.0);
        CHECK(std::abs(fa + fb - ff) <= 0.01 * ff);
    }
    // Mi-bande côté B (x ∈ [10,5 ; 11,5] mm, s ≈ 1 mm) : d_A ≈ 0,3.
    const double share =
        sewn_in_band(fills->a, 10'500.0, 11'500.0) / sewn_in_band(plain, 10'500.0, 11'500.0);
    CHECK(share == Catch::Approx(0.3).margin(0.12));
}

TEST_CASE("boundary blend: different angles overlap to about one plain layer", "[boundary_blend]") {
    const auto a = rect(0, 0, 10'000, 10'000);
    const auto b = rect(10'000, 0, 10'000, 10'000);
    const auto fills =
        fill_boundary_blend_tatami(a, params(0.0), b, params(1.5707963267948966), {});
    REQUIRE(fills.has_value());
    // Points de rangée seulement : rangées horizontales de A, verticales de B (liaisons exclues).
    const auto vertical = [](const std::vector<FillStitch>& f, double lo, double hi) {
        double sum = 0.0;
        for (std::size_t i = 1; i < f.size(); ++i) {
            const FillStitch& p = f[i - 1];
            const FillStitch& q = f[i];
            if (!q.jump && !q.travel && !p.travel && p.pos.x.value == q.pos.x.value &&
                p.pos.x.value >= lo && p.pos.x.value < hi) {
                sum += std::abs(static_cast<double>(q.pos.y.value - p.pos.y.value));
            }
        }
        return sum;
    };
    const auto plain = fill_tatami(rect(0, 0, 20'000, 10'000), params());
    const double sum =
        sewn_in_band(fills->a, 7'500.0, 12'500.0) + vertical(fills->b, 7'500.0, 12'500.0);
    const double ref = sewn_in_band(plain, 7'500.0, 12'500.0);
    INFO("A+B " << sum << " plain " << ref);
    CHECK(sum >= 0.85 * ref);
    CHECK(sum <= 1.15 * ref);
}

TEST_CASE("boundary blend: refusals carry stable codes", "[boundary_blend]") {
    const auto a = rect(0, 0, 10'000, 10'000);
    const auto far = rect(20'000, 0, 10'000, 10'000);
    const auto none = plan_boundary_blend(a, far, BoundaryBlendSpec{});
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error().code == "E_BLEND_NO_FRONTIER");

    BoundaryBlendSpec wide;
    wide.width_b = Micrometers{20'000};
    const auto bad = plan_boundary_blend(a, rect(10'000, 0, 10'000, 10'000), wide);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == "E_BLEND_WIDTH_RANGE");
}

TEST_CASE("boundary blend: oblique frontier and determinism", "[boundary_blend]") {
    // Frontière en diagonale : A triangle bas-gauche, B triangle haut-droit d'un carré.
    const auto node = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{
            Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner, {}, {}};
    };
    geometry::PathSet a;
    a.outer.closed = true;
    a.outer.nodes = {node(0, 0), node(16'000, 0), node(0, 16'000)};
    geometry::PathSet b;
    b.outer.closed = true;
    b.outer.nodes = {node(16'000, 0), node(16'000, 16'000), node(0, 16'000)};
    const auto plan = plan_boundary_blend(a, b, BoundaryBlendSpec{});
    REQUIRE(plan.has_value());
    // Sur la frontière, mi-densité ; à 2,5 mm perpendiculairement de part et d'autre, aplats.
    CHECK(plan->field->density_a(at(8'000, 8'000)) == Catch::Approx(0.5).margin(0.02));
    CHECK(plan->field->density_a(at(6'232, 6'232)) == Catch::Approx(1.0).margin(0.02));
    CHECK(plan->field->density_a(at(9'768, 9'768)) == Catch::Approx(0.0).margin(0.02));

    const auto f1 = fill_boundary_blend_tatami(a, params(0.3), b, params(0.3), {});
    const auto f2 = fill_boundary_blend_tatami(a, params(0.3), b, params(0.3), {});
    REQUIRE(f1.has_value());
    REQUIRE(f2.has_value());
    CHECK(f1->a == f2->a);
    CHECK(f1->b == f2->b);
}
