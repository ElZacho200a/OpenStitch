// SPDX-License-Identifier: Apache-2.0
// Tests du moteur d'auto-satin par squelette (specs/plans/satin-squelette-traversees.md) :
// corde, orientation, axe lissé, échantillonnage à pas adaptatif.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>

#include "axis.hpp"
#include "axis_sampler.hpp"
#include "chord.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/auto_satin/skeleton_satin.hpp"
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

namespace {

namespace as = openstitch::auto_satin;

struct Tri {
    P2 a, b, c;
};

bool in_tri(const Tri& t, P2 p) {
    const auto sign = [](P2 p1, P2 p2, P2 p3) {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    };
    const double d1 = sign(p, t.a, t.b);
    const double d2 = sign(p, t.b, t.c);
    const double d3 = sign(p, t.c, t.a);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0;
    const bool pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

P2 to_p(openstitch::Vec2um v) {
    return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
}

// Part de la région (échantillonnée sur une grille de 100 µm) balayée par les
// fils : triangles formés par deux traversées consécutives d'une même colonne.
double coverage_of(const openstitch::geometry::PathSet& region, const as::SkeletonSatinResult& r) {
    const auto polys = region_polys(region);
    std::vector<Tri> tris;
    for (const auto& col : r.columns) {
        for (std::size_t i = 0; i + 1 < col.crossings.size(); ++i) {
            const P2 a0 = to_p(col.crossings[i].a), b0 = to_p(col.crossings[i].b);
            const P2 a1 = to_p(col.crossings[i + 1].a), b1 = to_p(col.crossings[i + 1].b);
            tris.push_back({a0, b0, b1});
            tris.push_back({a0, b1, a1});
        }
    }
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (const auto& poly : polys) {
        for (const auto& q : poly) {
            minx = std::min(minx, q.x);
            maxx = std::max(maxx, q.x);
            miny = std::min(miny, q.y);
            maxy = std::max(maxy, q.y);
        }
    }
    long inside = 0, covered = 0;
    for (double y = miny + 50; y < maxy; y += 100.0) {
        for (double x = minx + 50; x < maxx; x += 100.0) {
            const P2 p{x, y};
            if (!in_region(polys, p)) {
                continue;
            }
            ++inside;
            for (const auto& t : tris) {
                if (in_tri(t, p)) {
                    ++covered;
                    break;
                }
            }
        }
    }
    return inside > 0 ? static_cast<double>(covered) / static_cast<double>(inside) : 0.0;
}

// Aire totale des quadrilatères de fils rapportée à l'aire de la région : 1 = pas de
// recouvrement, 1,3 = 30 % de fil en double.
double overlap_ratio(const openstitch::geometry::PathSet& region,
                     const as::SkeletonSatinResult& r) {
    double area = 0.0;
    const auto polys = region_polys(region);
    for (std::size_t k = 0; k < polys.size(); ++k) {
        double a = 0.0;
        const auto& poly = polys[k];
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const P2 p = poly[i], q = poly[(i + 1) % poly.size()];
            a += p.x * q.y - q.x * p.y;
        }
        area += (k == 0 ? 1.0 : -1.0) * std::abs(a) * 0.5;
    }
    double quads = 0.0;
    for (const auto& col : r.columns) {
        for (std::size_t i = 0; i + 1 < col.crossings.size(); ++i) {
            const P2 a0 = to_p(col.crossings[i].a), b0 = to_p(col.crossings[i].b);
            const P2 a1 = to_p(col.crossings[i + 1].a), b1 = to_p(col.crossings[i + 1].b);
            const auto tri = [](P2 x, P2 y, P2 z) {
                return std::abs((y.x - x.x) * (z.y - x.y) - (z.x - x.x) * (y.y - x.y)) * 0.5;
            };
            quads += tri(a0, b0, b1) + tri(a0, b1, a1);
        }
    }
    return area > 0.0 ? quads / area : 0.0;
}

} // namespace

namespace {

using openstitch::geometry::PathSet;

// Applique une transformation de coordonnées à tous les noeuds d'une région.
template <typename F> PathSet transformed(const PathSet& region, F f) {
    PathSet out = region;
    const auto apply = [&](openstitch::geometry::Path& path) {
        for (auto& n : path.nodes) {
            const auto [x, y] = f(static_cast<std::int32_t>(n.pos.x.value),
                                  static_cast<std::int32_t>(n.pos.y.value));
            n.pos.x = openstitch::Micrometers{x};
            n.pos.y = openstitch::Micrometers{y};
        }
        if (f(1, 0).first * f(0, 1).second - f(0, 1).first * f(1, 0).second < 0) {
            std::reverse(path.nodes.begin(), path.nodes.end()); // symétrie : garde l'orientation
        }
    };
    apply(out.outer);
    for (auto& h : out.holes) {
        apply(h);
    }
    return out;
}

as::SkeletonSatinResult run(const PathSet& region) {
    auto r = as::generate_skeleton_satin(region, {});
    REQUIRE(r.has_value());
    return std::move(*r);
}

bool same_result(const as::SkeletonSatinResult& a, const as::SkeletonSatinResult& b) {
    if (a.columns.size() != b.columns.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.columns.size(); ++i) {
        if (a.columns[i].crossings.size() != b.columns[i].crossings.size()) {
            return false;
        }
        for (std::size_t k = 0; k < a.columns[i].crossings.size(); ++k) {
            const auto& x = a.columns[i].crossings[k];
            const auto& y = b.columns[i].crossings[k];
            if (!(x.a == y.a) || !(x.b == y.b)) {
                return false;
            }
        }
    }
    return true;
}

struct Floor {
    const char* name;
    double coverage; // plancher de couverture (mesures du 2026-10, marge ~0,5 point)
    int columns;     // nombre de colonnes (branches) attendu
};

} // namespace

TEST_CASE("pipeline: planchers de couverture et recouvrement borne sur le corpus",
          "[skeleton_satin]") {
    // Planchers établis APRES mesure sur le corpus de formes historique (l'ancien
    // moteur plafonnait à 85,8-88,7 % sur y/t/cross/h/trident et n'avait jamais
    // réussi E, multi_neck ni star5). Ce sont des planchers de non-régression, pas
    // des seuils de qualité textile : aucune validation sur machine n'a eu lieu.
    const Floor floors[] = {
        {"rectangle", 0.990, 1},
        {"capsule", 0.990, 1},
        {"ribbon", 0.990, 1},
        {"s", 0.990, 1},
        {"y", 0.990, 3},
        {"y_symmetric", 0.990, 3},
        {"t", 0.990, 3},
        {"cross", 0.990, 4},
        {"h", 0.990, 5},
        {"wide", 0.990, 1},
        {"notch", 0.990, 1},
        {"pinch", 0.990, 1},
        {"trident", 0.980, 3},
        {"star5", 0.990, 7},
        {"asymmetric_star", 0.985, 7},
        {"comb", 0.980, 11},
        {"E", 0.985, 3},
        {"e_trunk_isolated", 0.985, 1},
        {"deep_recursive", 0.985, 3},
        {"multi_neck", 0.975, 1},
        {"dumbbell", 0.960, 1},
        {"two_holes", 0.985, 3},
        {"ring", 0.990, 1},
        {"ring_branch", 0.990, 1},
        {"junction_with_hole", 0.990, 6},
        {"polygonal_cut_fixture", 0.980, 3},
        {"thick_diagonal_blob", 0.960, 1},
    };
    for (const auto& f : floors) {
        INFO(f.name);
        const auto region = as::make_shape(f.name);
        REQUIRE(region.has_value());
        const auto res = run(*region);
        CHECK(static_cast<int>(res.columns.size()) == f.columns);
        CHECK(coverage_of(*region, res) >= f.coverage);
        const double ov = overlap_ratio(*region, res);
        CHECK(ov <= 1.10); // pas de fil en double notable
        CHECK(ov >= 0.90);
    }
}

TEST_CASE("pipeline: limite connue, les bras larges (blocs) ne sont pas couverts",
          "[skeleton_satin]") {
    // deep_channel : un U dont les bras font 16 mm de large. Ce ne sont pas des
    // rubans : l'élagage du squelette retire leurs branches courtes. Le moteur ne les
    // couvre donc pas ; l'éligibilité doit refuser cette forme (voir RD-PAT-004).
    const auto region = as::make_shape("deep_channel");
    REQUIRE(region.has_value());
    const auto res = run(*region);
    CHECK(coverage_of(*region, res) < 0.80);
}

TEST_CASE("pipeline: formes compactes refusees avec un message explicite", "[skeleton_satin]") {
    for (const char* name : {"disc_15mm", "circle", "petal", "disc_tight_inner_ring"}) {
        INFO(name);
        const auto region = as::make_shape(name);
        REQUIRE(region.has_value());
        const auto res = run(*region);
        CHECK(res.columns.empty());
        CHECK_FALSE(res.diagnostics.messages.empty());
    }
}

TEST_CASE("pipeline: anneau, colonne fermee sans doublon ni saut a la couture",
          "[skeleton_satin]") {
    const auto region = as::make_shape("ring");
    REQUIRE(region.has_value());
    const auto res = run(*region);
    REQUIRE(res.columns.size() == 1);
    const auto& cr = res.columns[0].crossings;
    REQUIRE(cr.size() > 20);
    CHECK(cr.front().a == cr.back().a); // clôture : dernière traversée = première
    CHECK(cr.front().b == cr.back().b);
    // Pas de doublon à la couture avant la clôture : l'avant-dernière diffère de la première.
    CHECK_FALSE(cr[cr.size() - 2].a == cr.front().a);
}

TEST_CASE("pipeline: deterministe, deux executions identiques", "[skeleton_satin]") {
    for (const char* name : {"t", "star5", "ring", "e_trunk_isolated", "multi_neck"}) {
        INFO(name);
        const auto region = as::make_shape(name);
        REQUIRE(region.has_value());
        CHECK(same_result(run(*region), run(*region)));
    }
}

TEST_CASE("pipeline: la decision et la couverture sont invariantes par rotation et miroir",
          "[skeleton_satin]") {
    for (const char* name : {"t", "cross", "s", "y", "E"}) {
        INFO(name);
        const auto region = as::make_shape(name);
        REQUIRE(region.has_value());
        const auto base = run(*region);
        const double baseCov = coverage_of(*region, base);

        const auto rot = transformed(*region, [](std::int32_t x, std::int32_t y) {
            return std::pair<std::int32_t, std::int32_t>{-y, x};
        });
        const auto rotRes = run(rot);
        CHECK(rotRes.columns.size() == base.columns.size());
        CHECK(coverage_of(rot, rotRes) == Approx(baseCov).margin(0.02));

        const auto mir = transformed(*region, [](std::int32_t x, std::int32_t y) {
            return std::pair<std::int32_t, std::int32_t>{-x, y};
        });
        const auto mirRes = run(mir);
        CHECK(mirRes.columns.size() == base.columns.size());
        CHECK(coverage_of(mir, mirRes) == Approx(baseCov).margin(0.02));
    }
}

TEST_CASE("pipeline: un guide absolu change l'orientation sans perdre la couverture",
          "[skeleton_satin]") {
    // Rectangle long : sans guide les traversées sont perpendiculaires à l'axe
    // (verticales). Un guide relatif de 30° incline toutes les traversées.
    const auto region = as::make_shape("rectangle");
    REQUIRE(region.has_value());
    // Guide au centre de la boîte englobante de la région.
    std::int64_t sx = 0, sy = 0;
    for (const auto& n : region->outer.nodes) {
        sx += n.pos.x.value;
        sy += n.pos.y.value;
    }
    const auto count = static_cast<std::int64_t>(region->outer.nodes.size());
    as::SkeletonSatinParameters prm;
    prm.guides.push_back(
        {openstitch::Vec2um{openstitch::Micrometers{static_cast<std::int32_t>(sx / count)},
                            openstitch::Micrometers{static_cast<std::int32_t>(sy / count)}},
         30.0 * std::numbers::pi / 180.0, false});
    const auto guided = as::generate_skeleton_satin(*region, prm);
    REQUIRE(guided.has_value());
    REQUIRE(guided->columns.size() == 1);
    CHECK(guided->diagnostics.orphan_guides == 0);
    const auto& mid = guided->columns[0].crossings[guided->columns[0].crossings.size() / 2];
    const double dx = static_cast<double>(mid.b.x.value - mid.a.x.value);
    const double dy = static_cast<double>(mid.b.y.value - mid.a.y.value);
    // Écart à la verticale ≈ 30°.
    CHECK(std::abs(std::atan2(std::abs(dx), std::abs(dy))) ==
          Approx(30.0 * std::numbers::pi / 180.0).margin(0.05));
    CHECK(coverage_of(*region, *guided) >= 0.90);
}

TEST_CASE("pipeline: un guide trop loin de l'axe est signale orphelin", "[skeleton_satin]") {
    const auto region = as::make_shape("rectangle");
    REQUIRE(region.has_value());
    as::SkeletonSatinParameters prm;
    prm.guides.push_back(
        {openstitch::Vec2um{openstitch::Micrometers{0}, openstitch::Micrometers{500'000}}, 0.3,
         false});
    const auto res = as::generate_skeleton_satin(*region, prm);
    REQUIRE(res.has_value());
    CHECK(res->diagnostics.orphan_guides == 1);
}
