// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "openstitch/stitch_generation/running_stitch.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

geometry::PathNode corner(std::int32_t x, std::int32_t y) {
    return {Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner, std::nullopt,
            std::nullopt};
}

geometry::Path open_path(std::initializer_list<std::pair<std::int32_t, std::int32_t>> pts) {
    geometry::Path p;
    p.closed = false;
    for (const auto& [x, y] : pts) {
        p.nodes.push_back(corner(x, y));
    }
    return p;
}

// Cercle approxime par un polygone regulier a `sides` cotes, rayon r (µm).
geometry::Path circle(double r, int sides, bool closed = true) {
    geometry::Path p;
    p.closed = closed;
    for (int i = 0; i < sides; ++i) {
        const double a = 2.0 * std::numbers::pi * i / sides;
        p.nodes.push_back(corner(static_cast<std::int32_t>(std::lround(r * std::cos(a))),
                                 static_cast<std::int32_t>(std::lround(r * std::sin(a)))));
    }
    return p;
}

double max_gap(const std::vector<Vec2um>& pts) {
    double m = 0.0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        m = std::max(m, length_um(pts[i] - pts[i - 1]));
    }
    return m;
}
double min_gap(const std::vector<Vec2um>& pts) {
    double m = std::numeric_limits<double>::max();
    for (std::size_t i = 1; i < pts.size(); ++i) {
        m = std::min(m, length_um(pts[i] - pts[i - 1]));
    }
    return pts.size() < 2 ? 0.0 : m;
}

}  // namespace

// --- D1 : espacement par longueur d'arc sur les courbes ----------------------

TEST_CASE("D1 corrige : cercle finement facette -> espacement = longueur cible") {
    // Cercle r=10mm en 64 cotes (arete ~0,98mm). Avant : 64 points a 0,98mm.
    RunningConfig cfg;
    cfg.target_length = Micrometers{3'000};
    cfg.corner_threshold = Angle{0.6108652};  // 35° : les micro-facettes ne sont PAS des coins
    const auto res = run_stitch(circle(10'000, 64), cfg);

    // Perimetre ~62,8mm / 3mm -> ~21 points (fermeture incluse : front==back).
    CHECK(res.points.size() >= 20);
    CHECK(res.points.size() <= 24);
    // Espacements proches de 3mm partout (et surtout PAS 0,98mm).
    CHECK(max_gap(res.points) < 3'400.0);
    CHECK(min_gap(res.points) > 2'600.0);
}

TEST_CASE("D2 corrige : micro-segments -> pas de rafale de points courts") {
    // 200 sommets espaces de 0,1mm, quasi droits. Avant : 200 points a 0,1mm.
    geometry::Path p;
    p.closed = false;
    for (int i = 0; i <= 200; ++i) {
        p.nodes.push_back(corner(i * 100, 0));
    }
    RunningConfig cfg;
    cfg.target_length = Micrometers{3'000};
    cfg.min_length = Micrometers{500};
    const auto res = run_stitch(p, cfg);
    CHECK(min_gap(res.points) >= 500.0);  // aucun ecart < min_length
    CHECK(max_gap(res.points) <= 3'100.0);
}

// --- D3 : Bezier suivie ------------------------------------------------------

TEST_CASE("D3 corrige : une Bezier est suivie, pas coupee en ligne droite") {
    const double R = 10'000.0;
    const double k = 0.5523;
    geometry::Path p;
    p.closed = false;
    geometry::PathNode a = corner(10'000, 0);
    a.tan_out = Vec2um{Micrometers{0}, Micrometers{static_cast<std::int32_t>(k * R)}};
    geometry::PathNode b = corner(0, 10'000);
    b.tan_in = Vec2um{Micrometers{static_cast<std::int32_t>(k * R)}, Micrometers{0}};
    p.nodes = {a, b};

    const auto res = run_stitch(p, {});
    // Les points intermediaires doivent etre a ~R du centre (sur la courbe),
    // et non sur la corde droite (qui passerait a ~7071 du centre au milieu).
    bool anyFarFromChord = false;
    for (const Vec2um& pt : res.points) {
        const double r = std::hypot(static_cast<double>(pt.x.value),
                                    static_cast<double>(pt.y.value));
        if (r > 9'000.0) {
            anyFarFromChord = true;
        }
    }
    CHECK(anyFarFromChord);
}

// --- D4 : resultat structure, jamais de crash --------------------------------

TEST_CASE("D4 corrige : chemin d'un seul noeud -> warning, pas de crash") {
    geometry::Path p;
    p.closed = false;
    p.nodes = {corner(0, 0)};
    const auto res = run_stitch(p, {});
    CHECK(res.points.size() <= 1);
    REQUIRE_FALSE(res.warnings.empty());
    CHECK(res.warnings.front().code == WarningCode::PathTooShort);
}

TEST_CASE("chemin vide -> warning PathEmpty") {
    const auto res = run_stitch(geometry::Path{}, {});
    REQUIRE_FALSE(res.warnings.empty());
    CHECK(res.warnings.front().code == WarningCode::PathEmpty);
}

// --- §41 criteres d'acceptation ----------------------------------------------

TEST_CASE("segment droit : espacement equilibre, extremites exactes") {
    const auto res = run_stitch(open_path({{0, 0}, {10'000, 0}}), {});
    REQUIRE(res.points.size() == 5);  // 10mm / 3mm -> 4 pas de 2,5mm
    CHECK(res.points.front() == Vec2um{Micrometers{0}, Micrometers{0}});
    CHECK(res.points.back() == Vec2um{Micrometers{10'000}, Micrometers{0}});
}

TEST_CASE("segment 10,3mm : pas de dernier point minuscule") {
    const auto res = run_stitch(open_path({{0, 0}, {10'300, 0}}), {});
    // n = round(10,3/3) = 3 -> 4 points, pas ~3,43mm ; aucun residu court.
    CHECK(min_gap(res.points) > 2'000.0);
    CHECK(max_gap(res.points) < 3'600.0);
}

TEST_CASE("coin en L : le sommet est une penetration exacte") {
    const auto res = run_stitch(open_path({{0, 0}, {9'000, 0}, {9'000, 9'000}}), {});
    // Le coin (9000,0) doit apparaitre exactement.
    const bool hasCorner =
        std::find(res.points.begin(), res.points.end(),
                  Vec2um{Micrometers{9'000}, Micrometers{0}}) != res.points.end();
    CHECK(hasCorner);
}

TEST_CASE("chemin ferme : revient au depart, sans doublon final minuscule") {
    const auto res = run_stitch(circle(10'000, 4), {});  // carre incline (losange)
    REQUIRE(res.points.size() >= 4);
    CHECK(res.points.front() == res.points.back());       // fermeture
    // Le segment de fermeture n'est pas minuscule.
    const double closing = length_um(res.points.back() - res.points[res.points.size() - 2]);
    CHECK(closing > 2'000.0);
}

TEST_CASE("sens inverse : sequence renversee") {
    RunningConfig fwd;
    RunningConfig rev;
    rev.reverse = true;
    const auto a = run_stitch(open_path({{0, 0}, {12'000, 0}}), fwd);
    const auto b = run_stitch(open_path({{0, 0}, {12'000, 0}}), rev);
    REQUIRE(a.points.size() == b.points.size());
    CHECK(a.points.front() == b.points.back());
    CHECK(a.points.back() == b.points.front());
}

TEST_CASE("depart impose sur une boucle fermee") {
    RunningConfig cfg;
    cfg.start = Vec2um{Micrometers{-10'000}, Micrometers{0}};  // proche d'un sommet du cercle
    const auto res = run_stitch(circle(10'000, 8), cfg);
    REQUIRE(res.points.size() >= 3);
    // Le premier point est proche du depart demande.
    CHECK(length_um(res.points.front() - *cfg.start) < 4'000.0);
}

TEST_CASE("deterministe") {
    const auto a = run_stitch(circle(8'000, 40), {});
    const auto b = run_stitch(circle(8'000, 40), {});
    CHECK(a.points == b.points);
}

// --- §8-9 repetitions --------------------------------------------------------

TEST_CASE("bean stitch : chaque intervalle traverse exactement 3 fois") {
    const std::vector<Vec2um> pts = {{Micrometers{0}, Micrometers{0}},
                                     {Micrometers{3'000}, Micrometers{0}},
                                     {Micrometers{6'000}, Micrometers{0}}};
    const auto bean = apply_repeat_mode(pts, RepeatMode::BeanStitch, 3);
    // 2 intervalles x 3 passages + point de depart = 7 points, sans mouvement nul.
    REQUIRE(bean.size() == 7);
    CHECK(bean[0] == pts[0]);
    CHECK(bean[1] == pts[1]);
    CHECK(bean[2] == pts[0]);
    CHECK(bean[3] == pts[1]);
    CHECK(bean[4] == pts[2]);
    for (std::size_t i = 1; i < bean.size(); ++i) {
        CHECK(bean[i] != bean[i - 1]);  // aucun mouvement nul
    }
}

TEST_CASE("aller-retour : termine au depart") {
    const std::vector<Vec2um> pts = {{Micrometers{0}, Micrometers{0}},
                                     {Micrometers{3'000}, Micrometers{0}},
                                     {Micrometers{6'000}, Micrometers{0}}};
    const auto bnf = apply_repeat_mode(pts, RepeatMode::BackAndForth);
    REQUIRE(bnf.size() == 5);
    CHECK(bnf.front() == bnf.back());
}

TEST_CASE("backstitch : progresse et couvre, sans mouvement nul") {
    const std::vector<Vec2um> pts = {{Micrometers{0}, Micrometers{0}},
                                     {Micrometers{3'000}, Micrometers{0}},
                                     {Micrometers{6'000}, Micrometers{0}}};
    const auto bs = apply_repeat_mode(pts, RepeatMode::Backstitch);
    REQUIRE(bs.size() >= pts.size());
    CHECK(bs.back() == pts.back());
    for (std::size_t i = 1; i < bs.size(); ++i) {
        CHECK(bs[i] != bs[i - 1]);
    }
}
