// SPDX-License-Identifier: Apache-2.0
// Fondu à la frontière tatami–tatami (lot L1b) : docs/studio-fondu-frontiere-spec.md.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/stitch_generation/boundary_blend.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"
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
    // Superposition volontairement renforcée (×1,5 par couche dans la bande) : deux couches
    // indépendantes à leur part exacte laissaient le tissu nu par endroits (recette Studio).
    CHECK(sum >= 0.85 * ref);
    CHECK(sum <= 1.3 * ref);
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

// --- L1c : directionnel ------------------------------------------------------------------

namespace {

document::DirectionalFillParams directional_params(std::int32_t gx0, std::int32_t gy0,
                                                   std::int32_t gx1, std::int32_t gy1) {
    document::DirectionalFillParams p;
    p.inset = Micrometers{0};
    geometry::Path guide;
    guide.closed = false;
    guide.nodes = {geometry::PathNode{at(gx0, gy0), geometry::NodeType::Corner, {}, {}},
                   geometry::PathNode{at(gx1, gy1), geometry::NodeType::Corner, {}, {}}};
    p.guides = {guide};
    return p;
}

double sewn_length(const std::vector<FillStitch>& f, double lo, double hi) {
    double sum = 0.0;
    for (std::size_t i = 1; i < f.size(); ++i) {
        const FillStitch& p = f[i - 1];
        const FillStitch& q = f[i];
        if (q.jump || q.travel) {
            continue;
        }
        const double mid = 0.5 * (p.pos.x.value + q.pos.x.value);
        if (mid >= lo && mid < hi) {
            sum += std::hypot(static_cast<double>(q.pos.x.value - p.pos.x.value),
                              static_cast<double>(q.pos.y.value - p.pos.y.value));
        }
    }
    return sum;
}

} // namespace

TEST_CASE("directional row mask: null mask is identical to plain directional",
          "[boundary_blend][directional]") {
    const auto region = rect(0, 0, 20'000, 12'000);
    const auto p = directional_params(0, 2'000, 20'000, 10'000);
    CHECK(fill_directional(region, p, nullptr) == fill_directional(region, p));
}

TEST_CASE("directional row mask: half density keeps about half the lines",
          "[boundary_blend][directional]") {
    const auto region = rect(0, 0, 20'000, 12'000);
    const auto p = directional_params(0, 6'000, 20'000, 6'000);
    const DensityRowMask half([](Vec2um) { return 0.5; }, DensityRowMask::Slice::Lower);
    const double full = sewn_length(fill_directional(region, p), 0.0, 20'000.0);
    const double kept = sewn_length(fill_directional(region, p, &half), 0.0, 20'000.0);
    REQUIRE(full > 0.0);
    CHECK(kept / full == Catch::Approx(0.5).margin(0.12));
}

TEST_CASE("boundary blend: tatami against directional overlaps to about one layer",
          "[boundary_blend][directional]") {
    const auto a = rect(0, 0, 10'000, 10'000);
    const auto b = rect(10'000, 0, 10'000, 10'000);
    const BoundaryBlendFillParams pa{params(0.0)};
    const BoundaryBlendFillParams pb{directional_params(10'000, 0, 20'000, 10'000)};
    const auto fills = fill_boundary_blend(a, pa, b, pb, BoundaryBlendSpec{});
    REQUIRE(fills.has_value());
    REQUIRE_FALSE(fills->a.empty());
    REQUIRE_FALSE(fills->b.empty());
    // B ne descend pas dans A au-delà de la demi-bande (2,5 mm) plus un demi-point.
    for (const FillStitch& s : fills->b) {
        CHECK(s.pos.x.value >= 7'500 - 1'500 - 2);
    }
    const auto plainA = fill_tatami(a, params());
    const auto plainB = fill_directional(b, std::get<document::DirectionalFillParams>(pb));
    // Couverture des deux zones réunies ≈ celle des deux aplats, liaisons comprises.
    const double blended =
        sewn_length(fills->a, 7'500.0, 12'500.0) + sewn_length(fills->b, 7'500.0, 12'500.0);
    const double plain =
        sewn_length(plainA, 7'500.0, 12'500.0) + sewn_length(plainB, 7'500.0, 12'500.0);
    INFO("blended " << blended << " plain " << plain);
    CHECK(blended >= 0.75 * plain);
    CHECK(blended <= 1.3 * plain);
}

TEST_CASE("boundary blend: directional pair is deterministic", "[boundary_blend][directional]") {
    const auto a = rect(0, 0, 10'000, 10'000);
    const auto b = rect(10'000, 0, 10'000, 10'000);
    const BoundaryBlendFillParams pa{directional_params(0, 0, 10'000, 10'000)};
    const BoundaryBlendFillParams pb{directional_params(10'000, 10'000, 20'000, 0)};
    const auto f1 = fill_boundary_blend(a, pa, b, pb, BoundaryBlendSpec{});
    const auto f2 = fill_boundary_blend(a, pa, b, pb, BoundaryBlendSpec{});
    REQUIRE(f1.has_value());
    REQUIRE(f2.has_value());
    CHECK_FALSE(f1->a.empty());
    CHECK_FALSE(f1->b.empty());
    CHECK(f1->a == f2->a);
    CHECK(f1->b == f2->b);
}

// --- Qualité de couture le long de la frontière ---------------------------------------------

namespace {

struct SeamQuality {
    double longest_link{0.0}; // plus long trajet cousu hors rangée dans la bande (µm)
    double uncovered{0.0};    // part des cases de la bande loin de tout point cousu
};

// Trajet de liaison : suite de points cousus consécutifs (sauts exclus, trajets cachés compris :
// un trajet caché que rien ne recouvre reste visible) non parallèles aux rangées de leur couche.
// Couverture : case de 0,2 mm couverte si un point de RANGÉE passe à moins de 0,3 mm (fil
// ≈ 0,4 mm, rangées à 0,4 mm : un aplat couvre tout).
SeamQuality seam_quality(const std::vector<FillStitch>& a, double angle_a,
                         const std::vector<FillStitch>& b, double angle_b, double x0, double x1,
                         double y0, double y1) {
    SeamQuality q;
    struct Seg {
        double ax, ay, bx, by;
    };
    std::vector<Seg> rows;
    const auto inside = [&](double x, double y) {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1;
    };
    const auto scan = [&](const std::vector<FillStitch>& f, double angle) {
        const double ux = std::cos(angle);
        const double uy = std::sin(angle);
        double path = 0.0;
        for (std::size_t i = 1; i < f.size(); ++i) {
            const FillStitch& p = f[i - 1];
            const FillStitch& s = f[i];
            // Sauts et trajets cachés (recousus par la rangée suivante) : hors liaison.
            if (s.jump || s.travel || p.travel) {
                path = 0.0;
                continue;
            }
            const Seg g{static_cast<double>(p.pos.x.value), static_cast<double>(p.pos.y.value),
                        static_cast<double>(s.pos.x.value), static_cast<double>(s.pos.y.value)};
            const double dx = g.bx - g.ax;
            const double dy = g.by - g.ay;
            const double len = std::hypot(dx, dy);
            if (len < 1.0) {
                continue;
            }
            const bool rowStitch = !s.travel && std::abs(dx * uy - dy * ux) / len <= 0.05;
            if (rowStitch) {
                rows.push_back(g);
                path = 0.0;
                continue;
            }
            path += len;
            if (inside(0.5 * (g.ax + g.bx), 0.5 * (g.ay + g.by))) {
                q.longest_link = std::max(q.longest_link, path);
            }
        }
    };
    scan(a, angle_a);
    scan(b, angle_b);
    int total = 0;
    int empty = 0;
    for (double y = y0; y <= y1; y += 200.0) {
        for (double x = x0; x <= x1; x += 200.0) {
            ++total;
            bool hit = false;
            for (const Seg& g : rows) {
                const double dx = g.bx - g.ax;
                const double dy = g.by - g.ay;
                const double l2 = dx * dx + dy * dy;
                double t = l2 > 0.0 ? ((x - g.ax) * dx + (y - g.ay) * dy) / l2 : 0.0;
                t = std::clamp(t, 0.0, 1.0);
                if (std::hypot(g.ax + t * dx - x, g.ay + t * dy - y) <= 300.0) {
                    hit = true;
                    break;
                }
            }
            empty += hit ? 0 : 1;
        }
    }
    q.uncovered = total > 0 ? static_cast<double>(empty) / total : 0.0;
    return q;
}

geometry::PathSet polygon(std::initializer_list<std::pair<std::int32_t, std::int32_t>> pts) {
    geometry::PathSet set;
    set.outer.closed = true;
    for (const auto& [x, y] : pts) {
        set.outer.nodes.push_back(geometry::PathNode{at(x, y), geometry::NodeType::Corner, {}, {}});
    }
    return set;
}

} // namespace

TEST_CASE("boundary blend: seam has no long links nor bare fabric", "[boundary_blend][seam]") {
    // Frontière droite puis brisée (zigzag oblique), angles égaux ou différents.
    const auto a1 = rect(0, 0, 10'000, 10'000);
    const auto b1 = rect(10'000, 0, 10'000, 10'000);
    const auto a2 = polygon({{0, 0}, {9'000, 0}, {12'000, 5'000}, {9'000, 10'000}, {0, 10'000}});
    const auto b2 =
        polygon({{9'000, 0}, {20'000, 0}, {20'000, 10'000}, {9'000, 10'000}, {12'000, 5'000}});
    // Zones rentrées de leur retrait de bord : couloir de 0,4 mm entre elles.
    const auto a3 = rect(0, 0, 9'800, 10'000);
    const auto b3 = rect(10'200, 0, 9'800, 10'000);
    const std::vector<std::pair<const geometry::PathSet*, const geometry::PathSet*>> pairs{
        {&a1, &b1}, {&a2, &b2}, {&a3, &b3}};
    for (std::size_t shape = 0; shape < pairs.size(); ++shape) {
        const auto& a = *pairs[shape].first;
        const auto& b = *pairs[shape].second;
        for (const auto& [angleA, angleB] :
             {std::pair{0.0, 0.0}, std::pair{0.785, -0.52}, std::pair{0.0, 1.5707963267948966}}) {
            const auto fills = fill_boundary_blend_tatami(a, params(angleA), b, params(angleB), {});
            REQUIRE(fills.has_value());
            const auto q = seam_quality(fills->a, angleA, fills->b, angleB, 6'500.0, 14'500.0,
                                        1'000.0, 9'000.0);
            INFO("shape " << shape << " angles " << angleA << " / " << angleB << " : link "
                          << q.longest_link << " um, uncovered " << q.uncovered);
            // Pas de liaison visible plus longue qu'un pas de rangée en biais (≈ 2 mm), pas de
            // tissu nu (avant correction : liaisons de 5 mm, 3,7 % nu, couloir nu entre zones).
            CHECK(q.longest_link <= 2'300.0);
            CHECK(q.uncovered <= 0.01);
        }
    }
}

// --- Sens des rangées dans la bande (§6.3) : Shared, Rotating et Interlaced
// ------------------------

namespace {

using Rows = document::BoundaryBlendRows;

double wrap_row_angle(double d) {
    constexpr double kPi = 3.14159265358979323846;
    double r = std::fmod(d, kPi);
    if (r > 0.5 * kPi) {
        r -= kPi;
    } else if (r <= -0.5 * kPi) {
        r += kPi;
    }
    return r;
}

// Le segment [p ; q] suit-il l'orientation `angle` (écart de direction, modulo π, <= tol rad) ?
bool follows(const FillStitch& p, const FillStitch& q, double angle, double tol) {
    const double dx = static_cast<double>(q.pos.x.value - p.pos.x.value);
    const double dy = static_cast<double>(q.pos.y.value - p.pos.y.value);
    return std::abs(wrap_row_angle(std::atan2(dy, dx) - angle)) <= tol;
}

// Orientation attendue d'une rangée de la bande au milieu de [p ; q].
struct BandAngle {
    const BoundaryBlendField* field;
    Rows rows;
    double angle_a;
    double angle_b;
    [[nodiscard]] double at(const FillStitch& p, const FillStitch& q) const {
        if (rows == Rows::Shared) {
            return boundary_blend_shared_angle(angle_a, angle_b);
        }
        const Vec2um mid{Micrometers{(p.pos.x.value + q.pos.x.value) / 2},
                         Micrometers{(p.pos.y.value + q.pos.y.value) / 2}};
        if (rows == Rows::Interlaced) {
            return boundary_blend_cross_angle(*field, mid);
        }
        return boundary_blend_row_angle(*field, angle_a, angle_b, mid);
    }
};

// Longueur des points de RANGÉE de la bande (orientation attendue à 0,12 rad près : grille du
// champ et cordes des rangées courbes) découpée par la fenêtre [x0 ; x1] × [y0 ; y1].
double band_row_length(const std::vector<FillStitch>& f, const BandAngle& band, double x0,
                       double x1, double y0, double y1) {
    double sum = 0.0;
    for (std::size_t i = 1; i < f.size(); ++i) {
        const FillStitch& p = f[i - 1];
        const FillStitch& q = f[i];
        if (q.jump || q.travel || p.travel || p.pos == q.pos) {
            continue;
        }
        if (!follows(p, q, band.at(p, q), 0.12)) {
            continue;
        }
        // Découpage de Liang-Barsky.
        const double ax = p.pos.x.value;
        const double ay = p.pos.y.value;
        const double dx = q.pos.x.value - ax;
        const double dy = q.pos.y.value - ay;
        double t0 = 0.0;
        double t1 = 1.0;
        bool keep = true;
        for (const auto& [pp, qq] : {std::pair{-dx, ax - x0}, std::pair{dx, x1 - ax},
                                     std::pair{-dy, ay - y0}, std::pair{dy, y1 - ay}}) {
            if (pp == 0.0) {
                keep = keep && qq >= 0.0;
                continue;
            }
            const double r = qq / pp;
            if (pp < 0.0) {
                t0 = std::max(t0, r);
            } else {
                t1 = std::min(t1, r);
            }
        }
        if (keep && t1 > t0) {
            sum += (t1 - t0) * std::hypot(dx, dy);
        }
    }
    return sum;
}

// Qualité de couture générale : un point est une rangée s'il suit l'angle de sa zone (cœur) ou
// l'orientation attendue de la bande (même logique de liaisons et de couverture que
// `seam_quality`).
SeamQuality seam_quality_rows(const std::vector<FillStitch>& a, double angle_a,
                              const std::vector<FillStitch>& b, double angle_b,
                              const BandAngle& band, double x0, double x1, double y0, double y1) {
    SeamQuality q;
    std::vector<std::pair<FillStitch, FillStitch>> rows;
    const auto inside = [&](double x, double y) {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1;
    };
    const auto scan = [&](const std::vector<FillStitch>& f, double angle) {
        double path = 0.0;
        for (std::size_t i = 1; i < f.size(); ++i) {
            const FillStitch& p = f[i - 1];
            const FillStitch& s = f[i];
            if (s.jump || s.travel || p.travel) {
                path = 0.0;
                continue;
            }
            const double dx = static_cast<double>(s.pos.x.value - p.pos.x.value);
            const double dy = static_cast<double>(s.pos.y.value - p.pos.y.value);
            const double len = std::hypot(dx, dy);
            if (len < 1.0) {
                continue;
            }
            // Bande : 0,3 rad, car la dernière corde d'une rangée courbe qui aborde le contour en
            // biais est replacée à l'intérieur du contour (écart mesuré jusqu'à 0,27 rad) ; une
            // liaison entre rangées voisines reste presque perpendiculaire aux rangées.
            if (follows(p, s, angle, 0.05) || follows(p, s, band.at(p, s), 0.3)) {
                rows.emplace_back(p, s);
                path = 0.0;
                continue;
            }
            path += len;
            if (inside(0.5 * (p.pos.x.value + s.pos.x.value),
                       0.5 * (p.pos.y.value + s.pos.y.value))) {
                q.longest_link = std::max(q.longest_link, path);
            }
        }
    };
    scan(a, angle_a);
    scan(b, angle_b);
    int total = 0;
    int empty = 0;
    for (double y = y0; y <= y1; y += 200.0) {
        for (double x = x0; x <= x1; x += 200.0) {
            ++total;
            bool hit = false;
            for (const auto& [p, s] : rows) {
                const double ax = p.pos.x.value;
                const double ay = p.pos.y.value;
                const double dx = s.pos.x.value - ax;
                const double dy = s.pos.y.value - ay;
                const double l2 = dx * dx + dy * dy;
                double t = l2 > 0.0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0.0;
                t = std::clamp(t, 0.0, 1.0);
                if (std::hypot(ax + t * dx - x, ay + t * dy - y) <= 300.0) {
                    hit = true;
                    break;
                }
            }
            empty += hit ? 0 : 1;
        }
    }
    q.uncovered = total > 0 ? static_cast<double>(empty) / total : 0.0;
    return q;
}

struct RowsCase {
    geometry::PathSet a;
    geometry::PathSet b;
    // Fenêtre entièrement dans la bande commune (partition).
    double x0, x1, y0, y1;
};

std::vector<RowsCase> rows_cases() {
    return {
        {rect(0, 0, 10'000, 10'000), rect(10'000, 0, 10'000, 10'000), 8'000.0, 12'000.0, 1'000.0,
         9'000.0},
        {polygon({{0, 0}, {9'000, 0}, {12'000, 5'000}, {9'000, 10'000}, {0, 10'000}}),
         polygon({{9'000, 0}, {20'000, 0}, {20'000, 10'000}, {9'000, 10'000}, {12'000, 5'000}}),
         10'000.0, 11'000.0, 2'000.0, 8'000.0},
    };
}

constexpr std::pair<double, double> kRowsAngles[] = {{0.785, -0.52}, {0.0, 1.5707963267948966}};

} // namespace

TEST_CASE("boundary blend rows: shared, rotating and interlaced partition the band exactly",
          "[boundary_blend][rows]") {
    const auto cases = rows_cases();
    for (const Rows rows : {Rows::Shared, Rows::Rotating, Rows::Interlaced}) {
        for (std::size_t shape = 0; shape < cases.size(); ++shape) {
            const RowsCase& c = cases[shape];
            for (const auto& [angleA, angleB] : kRowsAngles) {
                BoundaryBlendSpec spec;
                spec.rows = rows;
                const auto plan = plan_boundary_blend(c.a, c.b, spec);
                REQUIRE(plan.has_value());
                const BandAngle band{plan->field.get(), rows, angleA, angleB};
                const auto fills =
                    fill_boundary_blend_tatami(c.a, params(angleA), c.b, params(angleB), spec);
                const auto ref =
                    boundary_blend_band_rows(c.a, params(angleA), c.b, params(angleB), spec);
                REQUIRE(fills.has_value());
                REQUIRE(ref.has_value());
                const double la = band_row_length(fills->a, band, c.x0, c.x1, c.y0, c.y1);
                const double lb = band_row_length(fills->b, band, c.x0, c.x1, c.y0, c.y1);
                const double lr = band_row_length(*ref, band, c.x0, c.x1, c.y0, c.y1);
                const auto q = seam_quality_rows(fills->a, angleA, fills->b, angleB, band, 6'500.0,
                                                 14'500.0, 1'000.0, 9'000.0);
                INFO("rows " << static_cast<int>(rows) << " shape " << shape << " angles " << angleA
                             << " / " << angleB << " : A " << la << " B " << lb << " plain " << lr
                             << " link " << q.longest_link << " uncovered " << q.uncovered);
                REQUIRE(lr > 0.0);
                CHECK(la > 0.0);
                CHECK(lb > 0.0);
                CHECK(std::abs(la + lb - lr) <= 0.03 * lr);
                CHECK(q.longest_link <= 2'300.0);
                CHECK(q.uncovered <= 0.01);
            }
        }
    }
}

TEST_CASE("boundary blend rows: rotating rows turn from angle A to angle B",
          "[boundary_blend][rows]") {
    const auto cases = rows_cases();
    for (std::size_t shape = 0; shape < cases.size(); ++shape) {
        const RowsCase& c = cases[shape];
        for (const auto& [angleA, angleB] : kRowsAngles) {
            BoundaryBlendSpec spec;
            spec.rows = Rows::Rotating;
            const auto plan = plan_boundary_blend(c.a, c.b, spec);
            const auto fills =
                fill_boundary_blend_tatami(c.a, params(angleA), c.b, params(angleB), spec);
            REQUIRE(plan.has_value());
            REQUIRE(fills.has_value());
            const BoundaryBlendField& field = *plan->field;
            const double mid = boundary_blend_shared_angle(angleA, angleB);
            // Écart médian à l'angle visé des points cousus d'au moins 0,8 mm (rangées : liaisons
            // minoritaires) dont le milieu tombe dans la tranche [s0 ; s1] de distance signée.
            const auto median_dev = [&](double s0, double s1, double target) {
                std::vector<double> dev;
                for (const auto* f : {&fills->a, &fills->b}) {
                    for (std::size_t i = 1; i < f->size(); ++i) {
                        const FillStitch& p = (*f)[i - 1];
                        const FillStitch& q = (*f)[i];
                        const double dx = static_cast<double>(q.pos.x.value - p.pos.x.value);
                        const double dy = static_cast<double>(q.pos.y.value - p.pos.y.value);
                        if (q.jump || q.travel || std::hypot(dx, dy) < 800.0) {
                            continue;
                        }
                        const Vec2um m{Micrometers{(p.pos.x.value + q.pos.x.value) / 2},
                                       Micrometers{(p.pos.y.value + q.pos.y.value) / 2}};
                        const double s = field.signed_distance(m);
                        if (s >= s0 && s <= s1 && m.y.value >= 1'000 && m.y.value <= 9'000) {
                            dev.push_back(wrap_row_angle(std::atan2(dy, dx) - target));
                        }
                    }
                }
                CHECK(dev.size() >= 5);
                if (dev.empty()) {
                    return 10.0;
                }
                std::sort(dev.begin(), dev.end());
                return dev[dev.size() / 2];
            };
            // Milieu de bande : u(1/2) = 1/2, angle médian ; à plus de 0,25 rad des deux angles.
            const double dMid = median_dev(-400.0, 400.0, mid);
            // Bords de bande (0,05 à 0,55 mm à l'intérieur) : u <= 0,033, soit <= 0,05 rad de
            // l'angle de la zone ; tolérance 0,15 rad (grille du champ à 0,4 mm, cordes).
            const double dA = median_dev(-2'450.0, -1'950.0, angleA);
            const double dB = median_dev(1'950.0, 2'450.0, angleB);
            INFO("shape " << shape << " angles " << angleA << " / " << angleB << " : mid " << dMid
                          << " edgeA " << dA << " edgeB " << dB);
            CHECK(std::abs(dMid) <= 0.2);
            CHECK(std::abs(wrap_row_angle(mid + dMid - angleA)) >= 0.25);
            CHECK(std::abs(wrap_row_angle(mid + dMid - angleB)) >= 0.25);
            CHECK(std::abs(dA) <= 0.15);
            CHECK(std::abs(dB) <= 0.15);
        }
    }
}

TEST_CASE("boundary blend rows: interlaced rows cross the frontier and switch colour inside",
          "[boundary_blend][rows]") {
    // Entrelacé : les rangées traversent la bande, chaque rangée passe de A à B là où d_A franchit
    // son seuil. Les bouts des morceaux de rangée de A s'étalent donc sur toute la largeur de la
    // bande (passé empiétant), au lieu de rangées entières d'une seule couleur.
    const auto cases = rows_cases();
    for (std::size_t shape = 0; shape < cases.size(); ++shape) {
        const RowsCase& c = cases[shape];
        for (const auto& [angleA, angleB] : kRowsAngles) {
            BoundaryBlendSpec spec;
            spec.rows = Rows::Interlaced;
            const auto plan = plan_boundary_blend(c.a, c.b, spec);
            const auto fills =
                fill_boundary_blend_tatami(c.a, params(angleA), c.b, params(angleB), spec);
            REQUIRE(plan.has_value());
            REQUIRE(fills.has_value());
            const BoundaryBlendField& field = *plan->field;
            const BandAngle band{plan->field.get(), Rows::Interlaced, angleA, angleB};
            // Bouts de chaque morceau de rangée de A (début et fin) tombant dans la bande, en
            // distance signée : les bouts au bord de bande (cœur de A) sont écartés par la tranche.
            std::vector<double> ends;
            const auto& f = fills->a;
            const auto isRow = [&](std::size_t i) { // segment [i-1 ; i]
                if (i == 0 || i >= f.size()) {
                    return false;
                }
                const FillStitch& p = f[i - 1];
                const FillStitch& q = f[i];
                return !q.jump && !q.travel && !p.travel && p.pos != q.pos &&
                       follows(p, q, band.at(p, q), 0.2);
            };
            const auto keep = [&](Vec2um pos) {
                if (pos.y.value < 1'000 || pos.y.value > 9'000) {
                    return;
                }
                const double sd = field.signed_distance(pos);
                if (sd > -2'200.0 && sd < 2'200.0) {
                    ends.push_back(sd);
                }
            };
            for (std::size_t i = 1; i < f.size(); ++i) {
                if (!isRow(i)) {
                    continue;
                }
                if (!isRow(i - 1)) {
                    keep(f[i - 1].pos);
                }
                if (!isRow(i + 1)) {
                    keep(f[i].pos);
                }
            }
            std::sort(ends.begin(), ends.end());
            INFO("shape " << shape << " angles " << angleA << " / " << angleB << " : "
                          << ends.size() << " ends");
            REQUIRE(ends.size() >= 10);
            const double lo = ends[ends.size() / 10];
            const double hi = ends[ends.size() * 9 / 10];
            INFO("10-90 % spread " << lo << " .. " << hi);
            // Bande de 5 mm : les bouts couvrent au moins 2,5 mm (déciles 10 et 90).
            CHECK(hi - lo >= 2'500.0);
        }
    }
    // Orientation : perpendiculaire à une frontière droite, des deux côtés.
    const auto plan = plan_boundary_blend(rect(0, 0, 10'000, 10'000),
                                          rect(10'000, 0, 10'000, 10'000), BoundaryBlendSpec{});
    REQUIRE(plan.has_value());
    for (const std::int32_t x : {8'000, 10'000, 12'000}) {
        const double angle =
            boundary_blend_cross_angle(*plan->field, Vec2um{Micrometers{x}, Micrometers{5'000}});
        CHECK(std::abs(wrap_row_angle(angle)) <= 1e-6);
    }
}

TEST_CASE("boundary blend rows: shared and rotating are deterministic, directional falls back",
          "[boundary_blend][rows]") {
    const auto a = rect(0, 0, 10'000, 10'000);
    const auto b = rect(10'000, 0, 10'000, 10'000);
    const BoundaryBlendSpec perZone;
    const auto base = fill_boundary_blend_tatami(a, params(0.785), b, params(-0.52), perZone);
    REQUIRE(base.has_value());
    for (const Rows rows : {Rows::Shared, Rows::Rotating, Rows::Interlaced}) {
        BoundaryBlendSpec spec;
        spec.rows = rows;
        const auto f1 = fill_boundary_blend_tatami(a, params(0.785), b, params(-0.52), spec);
        const auto f2 = fill_boundary_blend_tatami(a, params(0.785), b, params(-0.52), spec);
        REQUIRE(f1.has_value());
        REQUIRE(f2.has_value());
        CHECK(f1->a == f2->a);
        CHECK(f1->b == f2->b);
        CHECK(f1->a != base->a); // le réglage change bien la génération

        // Zone directionnelle : repli documenté sur PerZone.
        const BoundaryBlendFillParams pa{params(0.0)};
        const BoundaryBlendFillParams pb{directional_params(10'000, 0, 20'000, 10'000)};
        const auto fallback = fill_boundary_blend(a, pa, b, pb, spec);
        const auto reference = fill_boundary_blend(a, pa, b, pb, perZone);
        REQUIRE(fallback.has_value());
        REQUIRE(reference.has_value());
        CHECK(fallback->a == reference->a);
        CHECK(fallback->b == reference->b);
    }
    CHECK(boundary_blend_shared_angle(0.0, 1.5707963267948966) ==
          Catch::Approx(0.7853981633974483));
    CHECK(boundary_blend_shared_angle(0.785, -0.52) == Catch::Approx(0.1325));
    CHECK(boundary_blend_shared_angle(0.1, 3.0) ==
          Catch::Approx(0.1 + 0.5 * (2.9 - 3.14159265358979323846)));
}

// --- Plusieurs fondus par zone (§6.4) : jonction en T --------------------------------------------

namespace {

// A = moitié gauche (0..10 x 0..20 mm), B = quart haut-droit, C = quart bas-droit. Jonction en
// (10 ; 10) mm. Liens posés directement dans le document (réciproques, même réglage).
struct TJunction {
    document::Project project;
    ObjectId a, b, c;
    double angle_a, angle_b, angle_c;
};

ObjectId add_zone(document::Project& p, const geometry::PathSet& shape, double angle,
                  std::uint8_t red) {
    document::VectorObject v;
    v.id = p.object_ids.next();
    v.paths.push_back(shape);
    p.vector_objects.push_back(v);
    document::EmbroideryObject e;
    e.id = p.object_ids.next();
    e.name = "Zone";
    e.source_vector = v.id;
    e.rgb = {red, 0, 0};
    e.params = params(angle);
    p.embroidery_objects.push_back(e);
    return e.id;
}

void link_zones(document::Project& p, ObjectId x, ObjectId y, Rows rows) {
    document::BoundaryBlendLink l;
    l.rows = rows;
    document::set_boundary_blend_link(p.findEmbroidery(x)->boundary_blends, y, l);
    document::set_boundary_blend_link(p.findEmbroidery(y)->boundary_blends, x, l);
}

TJunction make_t(double angleA, double angleB, double angleC, Rows rows, bool linkBC) {
    TJunction t;
    t.angle_a = angleA;
    t.angle_b = angleB;
    t.angle_c = angleC;
    t.a = add_zone(t.project, rect(0, 0, 10'000, 20'000), angleA, 10);
    t.b = add_zone(t.project, rect(10'000, 10'000, 10'000, 10'000), angleB, 20);
    t.c = add_zone(t.project, rect(10'000, 0, 10'000, 10'000), angleC, 30);
    link_zones(t.project, t.a, t.b, rows);
    link_zones(t.project, t.a, t.c, rows);
    if (linkBC) {
        link_zones(t.project, t.b, t.c, rows);
    }
    return t;
}

std::vector<FillStitch> layer_of(const document::Project& p, ObjectId id) {
    const auto layer = boundary_blend_layer(p, *p.findEmbroidery(id));
    REQUIRE(layer.has_value());
    return layer->stitches;
}

// Longueur cousue (ni saut ni trajet caché) découpée par la fenêtre. `rowAngle` : seuls les
// points qui suivent cette orientation (0,05 rad : rangées, liaisons exclues) ; sinon tous.
double sewn_in_window(const std::vector<FillStitch>& f, double x0, double x1, double y0, double y1,
                      std::optional<double> rowAngle = std::nullopt) {
    double sum = 0.0;
    for (std::size_t i = 1; i < f.size(); ++i) {
        const FillStitch& p = f[i - 1];
        const FillStitch& q = f[i];
        if (q.jump || q.travel || p.travel || p.pos == q.pos) {
            continue;
        }
        if (rowAngle && !follows(p, q, *rowAngle, 0.05)) {
            continue;
        }
        const double ax = p.pos.x.value;
        const double ay = p.pos.y.value;
        const double dx = q.pos.x.value - ax;
        const double dy = q.pos.y.value - ay;
        double t0 = 0.0;
        double t1 = 1.0;
        bool keep = true;
        for (const auto& [pp, qq] : {std::pair{-dx, ax - x0}, std::pair{dx, x1 - ax},
                                     std::pair{-dy, ay - y0}, std::pair{dy, y1 - ay}}) {
            if (pp == 0.0) {
                keep = keep && qq >= 0.0;
                continue;
            }
            const double r = qq / pp;
            if (pp < 0.0) {
                t0 = std::max(t0, r);
            } else {
                t1 = std::min(t1, r);
            }
        }
        if (keep && t1 > t0) {
            sum += (t1 - t0) * std::hypot(dx, dy);
        }
    }
    return sum;
}

// Qualité de couture de plusieurs couches : un point est une rangée s'il suit l'angle d'une des
// zones (0,05 rad) ou l'orientation attendue de la bande d'un des couples (0,3 rad, cf.
// `seam_quality_rows`) ; liaisons et couverture comme `seam_quality`.
SeamQuality seam_quality_multi(const std::vector<std::vector<FillStitch>>& layers,
                               const std::vector<double>& angles,
                               const std::vector<BandAngle>& bands, double x0, double x1, double y0,
                               double y1) {
    SeamQuality q;
    std::vector<std::pair<FillStitch, FillStitch>> rows;
    const auto isRow = [&](const FillStitch& p, const FillStitch& s) {
        for (const double angle : angles) {
            if (follows(p, s, angle, 0.05)) {
                return true;
            }
        }
        for (const BandAngle& band : bands) {
            if (band.rows != Rows::PerZone && follows(p, s, band.at(p, s), 0.3)) {
                return true;
            }
        }
        return false;
    };
    for (const auto& f : layers) {
        double path = 0.0;
        for (std::size_t i = 1; i < f.size(); ++i) {
            const FillStitch& p = f[i - 1];
            const FillStitch& s = f[i];
            if (s.jump || s.travel || p.travel) {
                path = 0.0;
                continue;
            }
            const double dx = static_cast<double>(s.pos.x.value - p.pos.x.value);
            const double dy = static_cast<double>(s.pos.y.value - p.pos.y.value);
            const double len = std::hypot(dx, dy);
            if (len < 1.0) {
                continue;
            }
            if (isRow(p, s)) {
                rows.emplace_back(p, s);
                path = 0.0;
                continue;
            }
            path += len;
            const double mx = 0.5 * (p.pos.x.value + s.pos.x.value);
            const double my = 0.5 * (p.pos.y.value + s.pos.y.value);
            if (mx >= x0 && mx <= x1 && my >= y0 && my <= y1) {
                q.longest_link = std::max(q.longest_link, path);
            }
        }
    }
    int total = 0;
    int empty = 0;
    for (double y = y0; y <= y1; y += 200.0) {
        for (double x = x0; x <= x1; x += 200.0) {
            ++total;
            bool hit = false;
            for (const auto& [p, s] : rows) {
                const double ax = p.pos.x.value;
                const double ay = p.pos.y.value;
                const double dx = s.pos.x.value - ax;
                const double dy = s.pos.y.value - ay;
                const double l2 = dx * dx + dy * dy;
                double t = l2 > 0.0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0.0;
                t = std::clamp(t, 0.0, 1.0);
                if (std::hypot(ax + t * dx - x, ay + t * dy - y) <= 300.0) {
                    hit = true;
                    break;
                }
            }
            empty += hit ? 0 : 1;
        }
    }
    q.uncovered = total > 0 ? static_cast<double>(empty) / total : 0.0;
    return q;
}

struct TMeasure {
    SeamQuality seam;
    double sewn{0.0};   // longueur cousue des trois couches dans la fenêtre
    double plain{0.0};  // aplat du carré entier à l'angle de A, même fenêtre
    double single{0.0}; // un seul couple (A contre la moitié droite), même fenêtre
};

constexpr double kTx0 = 5'000.0;
constexpr double kTx1 = 15'000.0;
constexpr double kTy0 = 5'000.0;
constexpr double kTy1 = 15'000.0;

TMeasure measure_t(const TJunction& t, Rows rows) {
    const auto la = layer_of(t.project, t.a);
    const auto lb = layer_of(t.project, t.b);
    const auto lc = layer_of(t.project, t.c);
    // Champs des trois couples (orientation attendue des rangées de bande), sens canonique.
    BoundaryBlendSpec spec;
    spec.rows = rows;
    const auto ra = rect(0, 0, 10'000, 20'000);
    const auto rb = rect(10'000, 10'000, 10'000, 10'000);
    const auto rc = rect(10'000, 0, 10'000, 10'000);
    const auto pab = plan_boundary_blend(ra, rb, spec);
    const auto pac = plan_boundary_blend(ra, rc, spec);
    const auto pbc = plan_boundary_blend(rb, rc, spec);
    REQUIRE(pab.has_value());
    REQUIRE(pac.has_value());
    REQUIRE(pbc.has_value());
    const std::vector<BandAngle> bands{{pab->field.get(), rows, t.angle_a, t.angle_b},
                                       {pac->field.get(), rows, t.angle_a, t.angle_c},
                                       {pbc->field.get(), rows, t.angle_b, t.angle_c}};
    TMeasure m;
    m.seam = seam_quality_multi({la, lb, lc}, {t.angle_a, t.angle_b, t.angle_c}, bands, kTx0, kTx1,
                                kTy0, kTy1);
    // Points de rangée à l'angle de A seulement (mesure utile quand les trois angles sont égaux).
    const double ra0 = t.angle_a;
    m.sewn = sewn_in_window(la, kTx0, kTx1, kTy0, kTy1, ra0) +
             sewn_in_window(lb, kTx0, kTx1, kTy0, kTy1, ra0) +
             sewn_in_window(lc, kTx0, kTx1, kTy0, kTy1, ra0);
    m.plain = sewn_in_window(fill_tatami(rect(0, 0, 20'000, 20'000), params(t.angle_a)), kTx0, kTx1,
                             kTy0, kTy1, ra0);
    // Référence « un seul couple » : A contre la moitié droite entière, même réglage (son
    // recouvrement cœur-bande voulu est compté de la même façon).
    const auto single = fill_boundary_blend_tatami(
        ra, params(t.angle_a), rect(10'000, 0, 10'000, 20'000), params(t.angle_a), spec);
    REQUIRE(single.has_value());
    m.single = sewn_in_window(single->a, kTx0, kTx1, kTy0, kTy1, ra0) +
               sewn_in_window(single->b, kTx0, kTx1, kTy0, kTy1, ra0);
    return m;
}

} // namespace

TEST_CASE("multi blend: T junction seams have no long links nor bare fabric",
          "[boundary_blend][multi]") {
    struct Angles {
        double a, b, c;
    };
    for (const Rows rows : {Rows::PerZone, Rows::Shared, Rows::Rotating, Rows::Interlaced}) {
        for (const Angles ang : {Angles{0.0, 0.0, 0.0}, Angles{0.0, 0.785, -0.52}}) {
            for (const bool linkBC : {true, false}) {
                const auto t = make_t(ang.a, ang.b, ang.c, rows, linkBC);
                const TMeasure m = measure_t(t, rows);
                INFO("rows " << static_cast<int>(rows) << " angles " << ang.a << "/" << ang.b << "/"
                             << ang.c << " BC " << linkBC << " : link " << m.seam.longest_link
                             << " uncovered " << m.seam.uncovered << " sewn " << m.sewn << " plain "
                             << m.plain << " single " << m.single);
                CHECK(m.seam.longest_link <= 2'300.0);
                CHECK(m.seam.uncovered <= 0.01);
                const bool sameGrid = ang.b == ang.a && ang.c == ang.a;
                if (sameGrid && rows == Rows::PerZone) {
                    // Même angle et même écart : chaque morceau de rangée cousu une seule fois.
                    CHECK(std::abs(m.sewn - m.plain) <= 0.03 * m.plain);
                }
                if (sameGrid && rows != Rows::PerZone && !linkBC) {
                    // Modes « bande » : pas plus que le recouvrement cœur-bande voulu d'un couple
                    // seul (B et C sans fondu entre elles : même frontière qu'A | moitié droite).
                    CHECK(std::abs(m.sewn - m.single) <= 0.05 * m.single);
                }
            }
        }
    }
}

TEST_CASE("multi blend: the second blend of a zone changes its layer", "[boundary_blend][multi]") {
    for (const Rows rows : {Rows::PerZone, Rows::Shared}) {
        auto t = make_t(0.0, 0.785, -0.52, rows, false);
        const auto both = layer_of(t.project, t.a);
        // Retrait du lien A-C (des deux côtés) : A ne garde que le fondu A-B.
        document::set_boundary_blend_link(t.project.findEmbroidery(t.a)->boundary_blends, t.c,
                                          std::nullopt);
        document::set_boundary_blend_link(t.project.findEmbroidery(t.c)->boundary_blends, t.a,
                                          std::nullopt);
        const auto onlyAB = layer_of(t.project, t.a);
        CHECK(both != onlyAB);
        // A coud dans la demi-bande de C seulement quand A-C est en fondu.
        CHECK(sewn_in_window(both, 10'300.0, 12'500.0, 1'000.0, 8'000.0) > 0.0);
        CHECK(sewn_in_window(onlyAB, 10'300.0, 12'500.0, 1'000.0, 8'000.0) == 0.0);
    }
}

TEST_CASE("multi blend: group layers are deterministic and the group is listed",
          "[boundary_blend][multi]") {
    const auto t = make_t(0.0, 0.785, -0.52, Rows::Rotating, true);
    for (const ObjectId id : {t.a, t.b, t.c}) {
        CHECK(layer_of(t.project, id) == layer_of(t.project, id));
        const auto group = boundary_blend_group(t.project, id);
        REQUIRE(group.size() == 3);
        CHECK(group[0] == document::BoundaryBlendPair{t.a, t.b});
        CHECK(group[1] == document::BoundaryBlendPair{t.a, t.c});
        CHECK(group[2] == document::BoundaryBlendPair{t.b, t.c});
    }
}
