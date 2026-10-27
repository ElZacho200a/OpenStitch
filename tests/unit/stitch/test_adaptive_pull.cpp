// SPDX-License-Identifier: Apache-2.0
//
// AdaptivePullCompensation niveau 1 (`PullMode::Adaptive`) : compensation LOCALE du satin.
// Valide la GEOMETRIE et la robustesse du calcul, jamais une efficacite textile reelle.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "openstitch/stitch_generation/adaptive_pull.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

// Colonne droite le long de x, largeur `w` (µm) variable via `width_at(x)`.
template <class F> std::vector<SatinPullInput> straight_column(int n, double step_um, F width_at) {
    std::vector<SatinPullInput> v;
    for (int i = 0; i < n; ++i) {
        const double x = i * step_um;
        const double w = width_at(x);
        v.push_back({x, w / 2.0, x, -w / 2.0, false});
    }
    return v;
}

// Colonne en arc de rayon d'axe `r` (µm), largeur `w`, virage a gauche (axe tournant vers +y).
std::vector<SatinPullInput> arc_column(int n, double r, double w, double sweep) {
    std::vector<SatinPullInput> v;
    for (int i = 0; i < n; ++i) {
        const double t = sweep * i / (n - 1);
        // Axe : (r sin t, r (1 - cos t)) ; normale gauche : (-cos t... ) vers le centre.
        const double cx = r * std::sin(t);
        const double cy = r * (1.0 - std::cos(t));
        const double nx = -std::sin(t); // direction vers le centre de courbure (gauche)
        const double ny = std::cos(t);
        // A = cote gauche (interieur), B = cote droit (exterieur).
        v.push_back(
            {cx + nx * w / 2.0, cy + ny * w / 2.0, cx - nx * w / 2.0, cy - ny * w / 2.0, false});
    }
    return v;
}

} // namespace

TEST_CASE("adaptive satin pull is empty for empty input", "[adaptive_pull]") {
    REQUIRE(adaptive_satin_pull({}, AdaptiveSatinModel{}, 5'000.0).empty());
}

TEST_CASE("adaptive satin pull follows local width and relaxes at the ends", "[adaptive_pull]") {
    const auto col = straight_column(61, 250.0, [](double) { return 4'000.0; });
    const auto out = adaptive_satin_pull(col, AdaptiveSatinModel{}, 5'000.0);
    REQUIRE(out.size() == col.size());
    const SatinPullSample& mid = out[30];
    const SatinPullSample& end = out.front();
    REQUIRE(mid.left_um > 0.0);
    REQUIRE(mid.left_um == mid.right_um); // colonne droite : symetrique
    REQUIRE(end.left_um < mid.left_um);   // bout relaxe
    REQUIRE(out.back().left_um < mid.left_um);
    REQUIRE((mid.flags & kPullEndReduced) == 0); // plein milieu : pas de facteur de bout
    REQUIRE((end.flags & kPullEndReduced) != 0);
    // Largeur 4 mm : a + b*w = 0,05 + 0,16 = 0,21 mm = 210 um.
    REQUIRE_THAT(mid.left_um, Catch::Matchers::WithinAbs(210.0, 1.0));
}

TEST_CASE("adaptive satin pull grows with width but stays inside the model bounds",
          "[adaptive_pull]") {
    const AdaptiveSatinModel m{};
    const auto thin =
        adaptive_satin_pull(straight_column(61, 250.0, [](double) { return 1'000.0; }), m, 5'000.0);
    const auto wide =
        adaptive_satin_pull(straight_column(61, 250.0, [](double) { return 8'000.0; }), m, 5'000.0);
    REQUIRE(wide[30].left_um > thin[30].left_um);
    REQUIRE(wide[30].left_um <= m.max_mm * 1000.0 + 1e-6);
    REQUIRE(thin[30].left_um <= 0.2 * 1'000.0 + 1e-6); // jamais plus de 20 % de la largeur
}

TEST_CASE("adaptive satin pull varies smoothly along a widening column", "[adaptive_pull]") {
    // Largeur 1 mm -> 6 mm sur 15 mm : pas de marche brutale entre stations voisines.
    const auto col =
        straight_column(61, 250.0, [](double x) { return 1'000.0 + x * (5'000.0 / 15'000.0); });
    const auto out = adaptive_satin_pull(col, AdaptiveSatinModel{}, 5'000.0);
    double maxStep = 0.0;
    for (std::size_t i = 1; i < out.size(); ++i) {
        maxStep = std::max(maxStep, std::abs(out[i].left_um - out[i - 1].left_um));
    }
    REQUIRE(maxStep < 25.0); // < 25 um (0,025 mm) d'une station a l'autre
    REQUIRE(out[55].left_um > out[10].left_um);
}

TEST_CASE("adaptive satin pull caps the inner side of a tight curve", "[adaptive_pull]") {
    const AdaptiveSatinModel m{};
    // Rayon d'axe 2 mm, largeur 2 mm => rayon interieur 1 mm : plafond 0,4 mm * ... = 0,4 mm
    // mais la borne de largeur (20 % de 2 mm = 0,4 mm) et celle du rayon (0,4 * 1 mm = 0,4 mm)
    // coincident ; on prend un rayon plus serre pour que la courbure morde.
    const auto col = arc_column(41, 1'200.0, 2'000.0, std::numbers::pi * 0.9);
    const auto out = adaptive_satin_pull(col, m, 5'000.0);
    const SatinPullSample& s = out[20];
    // Rayon interieur = 1200 - 1000 = 200 um => plafond interieur = 0,4 * 200 = 80 um.
    REQUIRE(s.left_um <= 80.0 + 1.0); // A = interieur
    REQUIRE(s.right_um > s.left_um);  // l'exterieur n'est pas bride par le rayon
    REQUIRE((s.flags & kPullCurvatureCapped) != 0);
    REQUIRE(s.curvature_per_mm > 0.0);
}

TEST_CASE("adaptive satin pull never exceeds the hard cap", "[adaptive_pull]") {
    AdaptiveSatinModel m;
    m.a_mm = 5.0;
    m.max_mm = 5.0;
    m.min_mm = 5.0;
    const auto out =
        adaptive_satin_pull(straight_column(21, 250.0, [](double) { return 40'000.0; }), m, 300.0);
    for (const auto& s : out) {
        REQUIRE(s.left_um <= 300.0);
        REQUIRE(s.right_um <= 300.0);
    }
}

TEST_CASE("adaptive satin pull smoothing does not cross a jump", "[adaptive_pull]") {
    // Deux tronçons de largeurs très différentes séparés par un saut.
    auto col = straight_column(40, 250.0, [](double) { return 1'000.0; });
    auto wide = straight_column(40, 250.0, [](double) { return 8'000.0; });
    for (auto& s : wide) {
        s.ax += 20'000.0;
        s.bx += 20'000.0;
    }
    wide.front().jump_before = true;
    col.insert(col.end(), wide.begin(), wide.end());
    const auto out = adaptive_satin_pull(col, AdaptiveSatinModel{}, 5'000.0);
    // Au coeur de chaque tronçon, la valeur reflète SA largeur, pas la moyenne des deux.
    REQUIRE(out[20].left_um < out[60].left_um);
    REQUIRE((out[41].flags & kPullJunctionReduced) != 0);
    REQUIRE((out[20].flags & kPullJunctionReduced) == 0);
}

TEST_CASE("adaptive satin pull is deterministic", "[adaptive_pull]") {
    const auto col = arc_column(33, 3'000.0, 1'800.0, 1.2);
    const auto a = adaptive_satin_pull(col, AdaptiveSatinModel{}, 5'000.0);
    const auto b = adaptive_satin_pull(col, AdaptiveSatinModel{}, 5'000.0);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].left_um == b[i].left_um);
        REQUIRE(a[i].right_um == b[i].right_um);
        REQUIRE(a[i].flags == b[i].flags);
    }
}

TEST_CASE("adaptive satin pull model scale is a neutral multiplier at one", "[adaptive_pull]") {
    AdaptiveSatinModel base;
    AdaptiveSatinModel scaled;
    scaled.scale = 2.0;
    const auto col = straight_column(61, 250.0, [](double) { return 2'000.0; });
    const auto a = adaptive_satin_pull(col, base, 5'000.0);
    const auto b = adaptive_satin_pull(col, scaled, 5'000.0);
    REQUIRE(b[30].left_um > a[30].left_um);
    REQUIRE(b[30].left_um <= 2.0 * a[30].left_um + 1e-6);
}

TEST_CASE("fill satin adaptive widens the column locally and exposes the profile",
          "[adaptive_pull][satin]") {
    geometry::Path railA;
    geometry::Path railB;
    const auto n = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                  geometry::NodeType::Corner, std::nullopt, std::nullopt};
    };
    // Colonne qui s'evase de 2 mm a 6 mm sur 20 mm.
    railA.nodes = {n(0, 1'000), n(20'000, 3'000)};
    railB.nodes = {n(0, -1'000), n(20'000, -3'000)};

    SatinConfig plain;
    SatinConfig adaptive;
    adaptive.adaptive_pull = true;
    const SatinResult r0 = fill_satin(railA, railB, plain);
    const SatinResult r1 = fill_satin(railA, railB, adaptive);
    REQUIRE(r0.satin.size() == r1.satin.size());
    REQUIRE(r1.pull_profile.size() >= 2);
    REQUIRE(r0.pull_profile.empty());
    // Les points d'une traversee adaptative sont plus ecartes que les points nominaux.
    const auto span = [](const SatinResult& r, std::size_t i) {
        const double dx = r.satin[i + 1].x.value - r.satin[i].x.value;
        const double dy = r.satin[i + 1].y.value - r.satin[i].y.value;
        return std::hypot(dx, dy);
    };
    const std::size_t mid = (r0.satin.size() / 4) * 2; // debut d'une traversee
    REQUIRE(span(r1, mid) > span(r0, mid));
    // La compensation locale croit avec la largeur le long de la colonne.
    REQUIRE(r1.pull_profile[r1.pull_profile.size() * 3 / 4].left_um >
            r1.pull_profile[r1.pull_profile.size() / 4].left_um);
}

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

geometry::PathNode cnode(std::int32_t x, std::int32_t y) {
    return geometry::PathNode{um(x, y), geometry::NodeType::Corner, std::nullopt, std::nullopt};
}

geometry::Path square_path(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    geometry::Path p;
    p.closed = true;
    p.nodes = {cnode(x0, y0), cnode(x1, y0), cnode(x1, y1), cnode(x0, y1)};
    return p;
}

document::TatamiParams flat_tatami(document::PullMode mode) {
    document::TatamiParams p;
    p.inset = Micrometers{0};
    p.pull_mode = mode;
    return p;
}

std::pair<int, int> x_range(const std::vector<FillStitch>& fill) {
    int lo = 1'000'000;
    int hi = -1'000'000;
    for (const auto& f : fill) {
        lo = std::min(lo, f.pos.x.value);
        hi = std::max(hi, f.pos.x.value);
    }
    return {lo, hi};
}

} // namespace

TEST_CASE("adaptive fill end pull scales with row length and respects its caps",
          "[adaptive_pull][fill]") {
    const AdaptiveFillModel m{};
    FillEndContext shortRow{600.0, 400.0, 0.0, 0.0};
    FillEndContext longRow{20'000.0, 400.0, 0.0, 0.0};
    const double ps = adaptive_fill_end_pull(shortRow, m);
    const double pl = adaptive_fill_end_pull(longRow, m);
    REQUIRE(ps <= 0.15 * 600.0 + 1e-9); // jamais plus de 15 % d'une rangee minuscule
    REQUIRE(pl > ps);
    REQUIRE(pl <= m.max_mm * 1000.0 * 1.25 + 1e-9);
    REQUIRE(adaptive_fill_end_pull(FillEndContext{}, m) == 0.0);

    std::uint32_t flags = 0;
    FillEndContext tip = longRow;
    tip.overhang_um = 5'000.0;
    const double pt = adaptive_fill_end_pull(tip, m, &flags);
    REQUIRE(pt < pl);
    REQUIRE((flags & kFillTip) != 0);

    FillEndContext curved = longRow;
    curved.turning_rad = 3.0;
    REQUIRE(adaptive_fill_end_pull(curved, m) < pl);
}

TEST_CASE("tatami adaptive overshoots rows locally, ignores the manual value, is deterministic",
          "[adaptive_pull][tatami]") {
    const geometry::PathSet region{square_path(0, 0, 20'000, 10'000), {}};
    auto none = flat_tatami(document::PullMode::Manual);
    auto adaptive = flat_tatami(document::PullMode::Adaptive);
    adaptive.pull_compensation = Micrometers{2'500}; // ignore en mode adaptatif
    const auto plain = fill_tatami(region, none);
    const auto a = fill_tatami(region, adaptive);
    const auto b = fill_tatami(region, adaptive);
    REQUIRE(a == b);
    const auto [plo, phi] = x_range(plain);
    const auto [alo, ahi] = x_range(a);
    REQUIRE(alo < plo);
    REQUIRE(ahi > phi);
    REQUIRE(plo - alo < 600); // < 0,6 mm : la valeur manuelle de 2,5 mm n'est pas reprise
}

TEST_CASE("tatami adaptive never crosses over a narrow hole", "[adaptive_pull][tatami]") {
    // Trou de 0,3 mm de large (x = 9,85..10,15 mm) : chaque bout garde au plus la moitie.
    geometry::PathSet region{square_path(0, 0, 20'000, 10'000), {}};
    region.holes.push_back(square_path(9'850, 2'000, 10'150, 8'000));
    const auto fill = fill_tatami(region, flat_tatami(document::PullMode::Adaptive));
    REQUIRE_FALSE(fill.empty());
    int leftMax = -1'000'000;
    int rightMin = 1'000'000;
    for (const auto& f : fill) {
        if (f.jump || f.travel || f.pos.y.value < 2'200 || f.pos.y.value > 7'800) {
            continue;
        }
        if (f.pos.x.value < 10'000) {
            leftMax = std::max(leftMax, f.pos.x.value);
        } else {
            rightMin = std::min(rightMin, f.pos.x.value);
        }
    }
    REQUIRE(leftMax <= 10'000);
    REQUIRE(rightMin >= 10'000);
}

TEST_CASE("directional adaptive prolongs line ends and is deterministic",
          "[adaptive_pull][directional]") {
    const geometry::PathSet region{square_path(0, 0, 20'000, 10'000), {}};
    document::DirectionalFillParams plain;
    document::DirectionalFillParams adaptive;
    adaptive.pull_mode = document::PullMode::Adaptive;
    adaptive.pull_compensation = Micrometers{2'500}; // ignore
    const auto lp = directional_stitch_lines(region, plain);
    const auto la = directional_stitch_lines(region, adaptive);
    const auto lb = directional_stitch_lines(region, adaptive);
    REQUIRE(la == lb);
    REQUIRE(la.size() == lp.size());
    REQUIRE_FALSE(la.empty());
    double extraMax = 0.0;
    for (std::size_t i = 0; i < la.size(); ++i) {
        const double dx = la[i].front().x.value - lp[i].front().x.value;
        const double dy = la[i].front().y.value - lp[i].front().y.value;
        extraMax = std::max(extraMax, std::hypot(dx, dy));
    }
    REQUIRE(extraMax > 50.0);
    REQUIRE(extraMax < 700.0);
}
