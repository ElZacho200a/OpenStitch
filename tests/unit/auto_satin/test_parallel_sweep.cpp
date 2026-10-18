// SPDX-License-Identifier: Apache-2.0
// Balayage parallèle (lot D de docs/auto-satin-research.md) : bandes continues, aucune
// couture à travers le vide, angle global, déterminisme.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include "geometry_detail.hpp"
#include "openstitch/auto_satin/parallel_sweep.hpp"
#include "openstitch/auto_satin/satin_strategy.hpp"
#include "openstitch/auto_satin/shapes.hpp"

namespace as = openstitch::auto_satin;
namespace dt = openstitch::auto_satin::detail;

namespace {

as::SkeletonSatinParameters sweep_params() {
    as::SkeletonSatinParameters p;
    p.strategy = as::SatinStrategy::ParallelSweep;
    p.measure_coverage = true;
    return p;
}

as::SkeletonSatinResult sweep(const std::string& shape,
                              const as::SkeletonSatinParameters& p = sweep_params()) {
    const auto region = as::make_shape(shape);
    REQUIRE(region.has_value());
    const auto r = as::generate_parallel_sweep(*region, p);
    REQUIRE(r.has_value());
    return *r;
}

int crossings_of(const as::SkeletonSatinResult& r) {
    int n = 0;
    for (const auto& c : r.columns) {
        n += static_cast<int>(c.crossings.size());
    }
    return n;
}

} // namespace

TEST_CASE("sweep: un rectangle donne une seule bande de traversees paralleles",
          "[parallel_sweep]") {
    const auto r = sweep("rectangle");
    REQUIRE(r.columns.size() == 1);
    CHECK(crossings_of(r) > 10);
    CHECK(r.diagnostics.coverage_ratio > 0.98);
    CHECK(r.diagnostics.overlap_ratio < 1.15);
    // Toutes les traversees partagent la meme direction (angle global).
    const auto& cs = r.columns[0].crossings;
    const auto dir = [](const as::SkeletonSatinCrossing& c) {
        return std::atan2(double(c.b.y.value - c.a.y.value), double(c.b.x.value - c.a.x.value));
    };
    for (const auto& c : cs) {
        CHECK(std::abs(dir(c) - dir(cs.front())) < 1e-3);
    }
}

TEST_CASE("sweep: l'angle explicite est respecte", "[parallel_sweep]") {
    auto p = sweep_params();
    p.sweep_auto_angle = false;
    p.sweep_angle_rad = std::numbers::pi / 2.0; // traversees verticales
    const auto r = sweep("rectangle", p);
    REQUIRE_FALSE(r.columns.empty());
    for (const auto& col : r.columns) {
        for (const auto& c : col.crossings) {
            CHECK(std::abs(c.a.x.value - c.b.x.value) <= 1);
            CHECK(c.a.y.value != c.b.y.value);
        }
    }
}

TEST_CASE("sweep: les formes compactes sont couvertes sans refus", "[parallel_sweep]") {
    for (const char* name : {"circle", "disc_15mm", "petal"}) {
        INFO(name);
        const auto r = sweep(name);
        CHECK_FALSE(r.columns.empty());
        CHECK(r.diagnostics.coverage_ratio > 0.95);
    }
}

TEST_CASE("sweep: un trou separe les bandes et aucun fil ne le traverse", "[parallel_sweep]") {
    const auto region = as::make_shape("ring");
    REQUIRE(region.has_value());
    const auto polys = dt::region_polys(*region);
    const auto r = sweep("ring");
    CHECK(r.columns.size() >= 3); // haut, deux flancs, bas
    for (const auto& col : r.columns) {
        for (const auto& c : col.crossings) {
            for (int i = 1; i < 10; ++i) {
                const double t = i / 10.0;
                const dt::P2 p{c.a.x.value + t * (c.b.x.value - c.a.x.value),
                               c.a.y.value + t * (c.b.y.value - c.a.y.value)};
                CHECK(dt::in_region(polys, p));
            }
        }
    }
}

TEST_CASE("sweep: aucun fil ne sort de la region sur tout le corpus", "[parallel_sweep]") {
    for (const char* name : {"s", "y", "cross", "E", "comb", "star5", "two_holes", "deep_channel",
                             "multi_neck", "ring_branch"}) {
        INFO(name);
        const auto region = as::make_shape(name);
        REQUIRE(region.has_value());
        const auto polys = dt::region_polys(*region);
        const auto r = sweep(name);
        CHECK(crossings_of(r) > 0);
        int outside = 0;
        for (const auto& col : r.columns) {
            for (const auto& c : col.crossings) {
                for (int i = 1; i < 10; ++i) {
                    const double t = i / 10.0;
                    const dt::P2 p{c.a.x.value + t * (c.b.x.value - c.a.x.value),
                                   c.a.y.value + t * (c.b.y.value - c.a.y.value)};
                    outside += dt::in_region(polys, p) ? 0 : 1;
                }
            }
        }
        CHECK(outside == 0);
    }
}

TEST_CASE("sweep: deterministe et sans effet de bord", "[parallel_sweep]") {
    const auto a = sweep("comb");
    const auto b = sweep("comb");
    REQUIRE(a.columns.size() == b.columns.size());
    for (std::size_t i = 0; i < a.columns.size(); ++i) {
        REQUIRE(a.columns[i].crossings.size() == b.columns[i].crossings.size());
        for (std::size_t k = 0; k < a.columns[i].crossings.size(); ++k) {
            CHECK(a.columns[i].crossings[k].a == b.columns[i].crossings[k].a);
            CHECK(a.columns[i].crossings[k].b == b.columns[i].crossings[k].b);
        }
    }
}

namespace {

openstitch::Vec2um at_mm(double x, double y) {
    return {openstitch::Micrometers{static_cast<std::int32_t>(x * 1000.0)},
            openstitch::Micrometers{static_cast<std::int32_t>(y * 1000.0)}};
}

// Angle des fils (rad) de chaque traversee, dans l'ordre des colonnes.
std::vector<double> thread_angles(const as::SkeletonSatinResult& r) {
    std::vector<double> out;
    for (const auto& col : r.columns) {
        for (const auto& c : col.crossings) {
            out.push_back(
                std::atan2(double(c.b.y.value - c.a.y.value), double(c.b.x.value - c.a.x.value)));
        }
    }
    return out;
}

} // namespace

TEST_CASE("sweep: deux guides font tourner les fils de l'un a l'autre", "[parallel_sweep]") {
    // Rectangle 40 x 5 mm : fils verticaux (pi/2) sans guide ; balayage le long de x.
    auto p = sweep_params();
    p.guides.push_back({at_mm(5.0, 0.0), 0.0, false});  // perpendiculaire a l'axe
    p.guides.push_back({at_mm(35.0, 0.0), 0.5, false}); // +0,5 rad au bout
    const auto r = sweep("rectangle", p);
    CHECK(r.diagnostics.orphan_guides == 0);
    const auto angles = thread_angles(r);
    REQUIRE(angles.size() > 20);
    const double half = std::numbers::pi / 2.0;
    // Le balayage va de x = 40 mm (direction n = -x) vers x = 0 : il commence par le guide a
    // +0,5 rad (x = 35) et finit sur le guide a 0 (x = 5), chaque extremite gardant l'angle de
    // son guide le plus proche.
    CHECK(std::abs(angles.front() - (half + 0.5)) < 1e-2);
    CHECK(std::abs(angles.back() - half) < 1e-2);
    // Variation progressive d'un fil au suivant (pas de saut d'angle).
    for (std::size_t i = 1; i < angles.size(); ++i) {
        CHECK(std::abs(angles[i] - angles[i - 1]) < 0.05);
    }
    const double lo = *std::min_element(angles.begin(), angles.end());
    const double hi = *std::max_element(angles.begin(), angles.end());
    CHECK(lo >= half - 1e-2);
    CHECK(hi <= half + 0.5 + 1e-2);
}

TEST_CASE("sweep: des fils inclines ne se croisent pas et couvrent la forme", "[parallel_sweep]") {
    auto p = sweep_params();
    p.guides.push_back({at_mm(5.0, 0.0), 0.0, false});
    p.guides.push_back({at_mm(35.0, 0.0), 0.8, false});
    const auto r = sweep("rectangle", p);
    const auto m = as::measure_result(r);
    CHECK(m.crossing_pairs == 0);
    CHECK(m.coverage_ratio > 0.95);
    CHECK(m.overlap_ratio < 1.35);
}

TEST_CASE("sweep: un guide absolu fixe l'angle dans le repere du dessin", "[parallel_sweep]") {
    auto p = sweep_params();
    p.guides.push_back({at_mm(20.0, 0.0), 1.0, true}); // un seul guide : angle constant
    const auto r = sweep("rectangle", p);
    const auto angles = thread_angles(r);
    REQUIRE_FALSE(angles.empty());
    for (const double a : angles) {
        CHECK(std::abs(a - 1.0) < 1e-2);
    }
    CHECK(as::measure_result(r).coverage_ratio > 0.95);
}

TEST_CASE("sweep: un ecart trop grand est borne et signale", "[parallel_sweep]") {
    auto p = sweep_params();
    p.guides.push_back({at_mm(20.0, 0.0), 1.5, false}); // ~86 degres : fils quasi paralleles
    const auto r = sweep("rectangle", p);
    CHECK(r.diagnostics.clamped_angle > 0);
    REQUIRE_FALSE(r.columns.empty());
    for (const double a : thread_angles(r)) {
        CHECK(std::abs(a - std::numbers::pi / 2.0) <= std::numbers::pi / 3.0 + 1e-2);
    }
}

TEST_CASE("sweep: un guide hors de la forme est orphelin et sans effet", "[parallel_sweep]") {
    auto p = sweep_params();
    p.guides.push_back({at_mm(500.0, 0.0), 0.7, false});
    const auto r = sweep("rectangle", p);
    CHECK(r.diagnostics.orphan_guides == 1);
    REQUIRE_FALSE(r.diagnostics.messages.empty());
    const auto plain = sweep("rectangle");
    REQUIRE(r.columns.size() == plain.columns.size());
    CHECK(thread_angles(r) == thread_angles(plain));
}

TEST_CASE("sweep: les guides sont deterministes", "[parallel_sweep]") {
    auto p = sweep_params();
    p.guides.push_back({at_mm(35.0, 0.0), 0.5, false});
    p.guides.push_back({at_mm(5.0, 0.0), -0.3, false});
    const auto a = sweep("capsule", p);
    const auto b = sweep("capsule", p);
    REQUIRE(a.columns.size() == b.columns.size());
    for (std::size_t i = 0; i < a.columns.size(); ++i) {
        REQUIRE(a.columns[i].crossings.size() == b.columns[i].crossings.size());
        for (std::size_t k = 0; k < a.columns[i].crossings.size(); ++k) {
            CHECK(a.columns[i].crossings[k].a == b.columns[i].crossings[k].a);
            CHECK(a.columns[i].crossings[k].b == b.columns[i].crossings[k].b);
        }
    }
}

TEST_CASE("sweep: la facade route la strategie, y compris pour une forme compacte",
          "[parallel_sweep]") {
    // Le squelette refuse un disque (region compacte) ; le balayage le couvre.
    const auto region = as::make_shape("circle");
    REQUIRE(region.has_value());
    const auto viaFacade = as::generate_satin(as::SatinStrategy::ParallelSweep, *region, {});
    const auto direct = as::generate_parallel_sweep(*region, sweep_params());
    REQUIRE(viaFacade.has_value());
    REQUIRE(direct.has_value());
    CHECK(viaFacade->columns.size() == direct->columns.size());
    CHECK_FALSE(viaFacade->columns.empty());
    const auto skeleton = as::generate_satin(as::SatinStrategy::Skeleton, *region, {});
    REQUIRE(skeleton.has_value());
    CHECK(skeleton->columns.empty());
}
