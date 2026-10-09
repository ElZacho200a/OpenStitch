// SPDX-License-Identifier: Apache-2.0
// Tests du moteur d'auto-satin par squelette (specs/plans/satin-squelette-traversees.md) :
// corde, orientation, axe lissé, échantillonnage à pas adaptatif.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include "axis.hpp"
#include "axis_sampler.hpp"
#include "chord.hpp"
#include "orientation.hpp"

using namespace openstitch::auto_satin::detail;
using Catch::Approx;

namespace {

Poly rect(double x0, double y0, double x1, double y1) {
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

// Secteur d'anneau de rayons [r0, r1] entre les angles [a0, a1] (radians).
Poly ring_sector(double r0, double r1, double a0, double a1, int n) {
    Poly poly;
    for (int i = 0; i <= n; ++i) {
        const double a = a0 + (a1 - a0) * i / n;
        poly.push_back({r1 * std::cos(a), r1 * std::sin(a)});
    }
    for (int i = n; i >= 0; --i) {
        const double a = a0 + (a1 - a0) * i / n;
        poly.push_back({r0 * std::cos(a), r0 * std::sin(a)});
    }
    return poly;
}

std::vector<P2> line_axis(double x0, double x1, double y) {
    return {{x0, y}, {x1, y}};
}

} // namespace

TEST_CASE("chord: un rectangle donne un seul intervalle de la bonne longueur", "[skeleton_satin]") {
    const std::vector<Poly> polys{rect(0, 0, 20000, 4000)};
    const auto iv = line_intervals(polys, {10000, 2000}, {1, 0});
    REQUIRE(iv.size() == 1);
    CHECK(iv[0].length() == Approx(20000.0));
    CHECK(iv[0].t_lo == Approx(-10000.0));
}

TEST_CASE("chord: un trou donne deux intervalles et on choisit celui qui contient P",
          "[skeleton_satin]") {
    const std::vector<Poly> polys{rect(0, 0, 20000, 4000), rect(8000, 1000, 12000, 3000)};
    const auto all = line_intervals(polys, {2000, 2000}, {1, 0});
    REQUIRE(all.size() == 2);
    const auto left = chord_through(polys, {2000, 2000}, {1, 0}, 0.0);
    REQUIRE(left.has_value());
    CHECK(left->t_lo == Approx(-2000.0));
    CHECK(left->t_hi == Approx(6000.0)); // jusqu'au bord gauche du trou (x = 8000)
    const auto right = chord_through(polys, {18000, 2000}, {1, 0}, 0.0);
    REQUIRE(right.has_value());
    CHECK(right->t_lo == Approx(-6000.0));
    CHECK(right->t_hi == Approx(2000.0));
}

TEST_CASE("chord: une droite passant par deux sommets garde une parité correcte",
          "[skeleton_satin]") {
    // Losange |x| + |y| <= 1000 ; la droite y = 0 passe par ses sommets gauche et droit.
    const std::vector<Poly> polys{Poly{{1000, 0}, {0, 1000}, {-1000, 0}, {0, -1000}}};
    const auto iv = line_intervals(polys, {0, 0}, {1, 0});
    REQUIRE(iv.size() == 1);
    CHECK(iv[0].length() == Approx(2000.0));
}

TEST_CASE("chord: tolérance pour un point juste hors région", "[skeleton_satin]") {
    const std::vector<Poly> polys{rect(0, 0, 20000, 4000)};
    // Point 30 µm sous le bord bas : hors région, mais corde horizontale proche.
    CHECK_FALSE(chord_through(polys, {10000, -30}, {1, 0}, 0.0).has_value());
    // La droite horizontale à y = -30 ne rencontre rien : même avec tolérance, aucun intervalle.
    CHECK_FALSE(chord_through(polys, {10000, -30}, {1, 0}, 60.0).has_value());
    // Une droite verticale depuis ce point coupe la région 30 µm plus haut.
    const auto v = chord_through(polys, {10000, -30}, {0, 1}, 60.0);
    REQUIRE(v.has_value());
    CHECK(v->length() == Approx(4000.0));
}

TEST_CASE("orientation: ecart modulo pi par le chemin le plus court", "[skeleton_satin]") {
    const double deg = kPi / 180.0;
    CHECK(angle_diff_pi(170 * deg, 10 * deg) == Approx(20 * deg));
    CHECK(angle_diff_pi(10 * deg, 170 * deg) == Approx(-20 * deg));
    // Ambiguité à exactement 90° : rotation positive, dans les deux sens.
    CHECK(angle_diff_pi(0.0, 90 * deg) == Approx(90 * deg));
    CHECK(angle_diff_pi(90 * deg, 0.0) == Approx(90 * deg));
    CHECK(lerp_angle_pi(170 * deg, 10 * deg, 0.5) == Approx(0.0).margin(1e-9));
    CHECK(wrap_half_pi(kPi) == Approx(0.0).margin(1e-9));
}

TEST_CASE("orientation: sans cle, perpendiculaire a l'axe", "[skeleton_satin]") {
    OrientationKeys none;
    CHECK(orientation_at(none, 100.0, 0.3) == Approx(0.3 + kPi / 2.0));
}

TEST_CASE("orientation: deux cles absolues egales donnent un angle absolu constant",
          "[skeleton_satin]") {
    OrientationKeys k;
    k.keys = {{0.0, true, 0.4}, {1000.0, true, 0.4}};
    k.alpha = {0.0, 1.0};
    // L'axe tourne (alpha varie) mais l'angle absolu reste constant entre les cles.
    CHECK(orientation_at(k, 500.0, 0.5) == Approx(0.4));
    // Avant la premiere et apres la derniere cle : valeur de la cle.
    CHECK(orientation_at(k, -10.0, 0.0) == Approx(0.4));
    CHECK(orientation_at(k, 2000.0, 0.0) == Approx(0.4));
}

TEST_CASE("orientation: cles relatives interpolent l'ecart a la perpendiculaire",
          "[skeleton_satin]") {
    OrientationKeys k;
    const double deg = kPi / 180.0;
    k.keys = {{0.0, false, 0.0}, {1000.0, false, 30 * deg}};
    k.alpha = {0.0, 0.0};
    const double alpha = 0.2;
    CHECK(orientation_at(k, 500.0, alpha) == Approx(alpha + kPi / 2.0 + 15 * deg));
}

TEST_CASE("axe: une droite garde sa longueur et sa tangente", "[skeleton_satin]") {
    const Axis ax = Axis::build(line_axis(0, 10000, 0), {});
    REQUIRE_FALSE(ax.empty());
    CHECK(ax.length() == Approx(10000.0).margin(1.0));
    CHECK(ax.alpha(5000.0) == Approx(0.0).margin(1e-9));
    CHECK(ax.position(0.0).x == Approx(0.0));
    CHECK(ax.position(ax.length()).x == Approx(10000.0).margin(1.0));
}

TEST_CASE("axe: le lissage supprime l'escalier sans decaler les extremites", "[skeleton_satin]") {
    // Marches de 50 µm sur 20 mm : bruit de pixel.
    std::vector<P2> raw;
    for (int i = 0; i <= 400; ++i) {
        raw.push_back({i * 50.0, (i / 2 % 2) * 50.0});
    }
    const Axis ax = Axis::build(raw, {});
    CHECK(ax.position(0.0).x == Approx(0.0));
    CHECK(ax.position(ax.length()).x == Approx(20000.0));
    // Tangente ~ horizontale au milieu malgre l'escalier.
    CHECK(std::abs(ax.alpha(10000.0)) < 0.08);
}

TEST_CASE("echantillonnage: rectangle, traversees perpendiculaires et pas regulier",
          "[skeleton_satin]") {
    const std::vector<Poly> polys{rect(-10000, -2000, 10000, 2000)};
    const Axis ax = Axis::build(line_axis(-8000, 8000, 0), {});
    SamplerParams prm;
    prm.spacing_um = 400.0;
    const auto res = sample_axis(ax, polys, {}, prm);
    REQUIRE(res.samples.size() > 40);
    for (const auto& smp : res.samples) {
        CHECK(smp.length == Approx(4000.0).margin(1.0));
        CHECK(std::abs(smp.a.x - smp.b.x) < 1.0); // verticales
        CHECK(smp.a.y < smp.b.y);                 // A est du côté droit (-n), B du côté gauche
    }
    // Couverture des bouts : le prolongement atteint presque le bord.
    CHECK(res.samples.front().p.x < -9900.0);
    CHECK(res.samples.back().p.x > 9900.0);
    // Pas régulier sur l'axe droit (g' = 0, σ = ±1) : h = ρ.
    for (std::size_t i = 1; i + 1 < res.samples.size(); ++i) {
        CHECK(res.samples[i].s - res.samples[i - 1].s == Approx(400.0).margin(1.0));
    }
}

TEST_CASE("echantillonnage: virage, l'espacement du bord exterieur vaut rho", "[skeleton_satin]") {
    // Demi-anneau de rayons 3000..7000, axe à r = 5000 (largeur 4 mm, rayon de courbure 5 mm).
    const std::vector<Poly> polys{ring_sector(3000, 7000, 0.0, kPi, 400)};
    std::vector<P2> raw;
    for (int i = 0; i <= 300; ++i) {
        const double a = 0.04 + (kPi - 0.08) * i / 300.0;
        raw.push_back({5000.0 * std::cos(a), 5000.0 * std::sin(a)});
    }
    const Axis ax = Axis::build(raw, {});
    SamplerParams prm;
    prm.spacing_um = 400.0;
    const auto res = sample_axis(ax, polys, {}, prm);
    REQUIRE(res.samples.size() > 20);
    // A est du côté droit de l'axe parcouru en sens antihoraire : le côté extérieur.
    // Espacement entre extrémités extérieures consécutives ≈ ρ (à 8 % près).
    int checked = 0;
    for (std::size_t i = 6; i + 6 < res.samples.size(); ++i) {
        const double d = norm(res.samples[i].a - res.samples[i - 1].a);
        CHECK(d == Approx(400.0).epsilon(0.08));
        ++checked;
    }
    CHECK(checked > 10);
    // Le côté intérieur est plus dense que l'extérieur.
    const auto& mid = res.samples[res.samples.size() / 2];
    const auto& prev = res.samples[res.samples.size() / 2 - 1];
    CHECK(norm(mid.b - prev.b) < norm(mid.a - prev.a));
}

TEST_CASE("echantillonnage: deterministe, deux executions identiques", "[skeleton_satin]") {
    const std::vector<Poly> polys{ring_sector(3000, 7000, 0.0, kPi, 400)};
    std::vector<P2> raw;
    for (int i = 0; i <= 300; ++i) {
        const double a = 0.04 + (kPi - 0.08) * i / 300.0;
        raw.push_back({5000.0 * std::cos(a), 5000.0 * std::sin(a)});
    }
    const Axis ax = Axis::build(raw, {});
    const auto r1 = sample_axis(ax, polys, {}, {});
    const auto r2 = sample_axis(ax, polys, {}, {});
    REQUIRE(r1.samples.size() == r2.samples.size());
    for (std::size_t i = 0; i < r1.samples.size(); ++i) {
        CHECK(r1.samples[i].a.x == r2.samples[i].a.x);
        CHECK(r1.samples[i].b.y == r2.samples[i].b.y);
    }
}

TEST_CASE("echantillonnage: un angle absolu parallele a l'axe est ramene au plancher",
          "[skeleton_satin]") {
    const std::vector<Poly> polys{rect(-10000, -2000, 10000, 2000)};
    const Axis ax = Axis::build(line_axis(-8000, 8000, 0), {});
    OrientationKeys k;
    k.keys = {{0.0, true, 0.0}}; // horizontal = parallèle à l'axe
    k.alpha = {0.0};
    const auto res = sample_axis(ax, polys, k, {});
    REQUIRE_FALSE(res.samples.empty());
    CHECK(res.diagnostics.clamped_angle > 0);
    for (const auto& smp : res.samples) {
        CHECK(std::abs(std::sin(smp.g - smp.alpha)) >= 0.17 - 1e-9);
    }
}
