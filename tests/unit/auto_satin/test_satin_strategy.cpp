// SPDX-License-Identifier: Apache-2.0
// Façade des stratégies d'auto-satin et mesures comparables (lot A de
// docs/auto-satin-research.md) : la stratégie par défaut est EXACTEMENT le moteur actuel.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>

#include <string>
#include <vector>

#include "openstitch/auto_satin/satin_strategy.hpp"
#include "openstitch/auto_satin/shapes.hpp"

namespace as = openstitch::auto_satin;

namespace {

const std::vector<std::string> kCorpus = {"rectangle", "ribbon", "s",    "y",        "cross",
                                          "E",         "comb",   "ring", "two_holes"};

bool same_crossings(const as::SkeletonSatinResult& a, const as::SkeletonSatinResult& b) {
    if (a.columns.size() != b.columns.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.columns.size(); ++i) {
        const auto& x = a.columns[i].crossings;
        const auto& y = b.columns[i].crossings;
        if (x.size() != y.size()) {
            return false;
        }
        for (std::size_t k = 0; k < x.size(); ++k) {
            if (!(x[k].a == y[k].a) || !(x[k].b == y[k].b)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("strategy: le squelette est la seule strategie livree", "[satin_strategy]") {
    const auto all = as::available_strategies();
    REQUIRE(all.size() == 1);
    CHECK(all.front() == as::SatinStrategy::Skeleton);
    CHECK(as::strategy_id(as::SatinStrategy::Skeleton) == "skeleton");
    CHECK_FALSE(as::strategy_label(as::SatinStrategy::Skeleton).empty());
}

TEST_CASE("strategy: la facade reproduit generate_skeleton_satin octet pour octet",
          "[satin_strategy]") {
    for (const auto& name : kCorpus) {
        INFO(name);
        const auto region = as::make_shape(name);
        REQUIRE(region.has_value());
        const as::SkeletonSatinParameters params;
        const auto direct = as::generate_skeleton_satin(*region, params);
        const auto viaFacade = as::generate_satin(as::SatinStrategy::Skeleton, *region, params);
        REQUIRE(direct.has_value());
        REQUIRE(viaFacade.has_value());
        CHECK(same_crossings(*direct, *viaFacade));
    }
}

TEST_CASE("strategy: les mesures sont deterministes et coherentes", "[satin_strategy]") {
    for (const auto& name : kCorpus) {
        INFO(name);
        const auto region = as::make_shape(name);
        REQUIRE(region.has_value());
        const auto a = as::measure_satin(as::SatinStrategy::Skeleton, *region, {});
        const auto b = as::measure_satin(as::SatinStrategy::Skeleton, *region, {});
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(a->columns == b->columns);
        CHECK(a->crossings == b->crossings);
        CHECK(a->coverage_ratio == b->coverage_ratio);
        CHECK(a->overlap_peak == b->overlap_peak);
        CHECK(a->crossings > 0);
        CHECK(a->crossing_pairs == 0); // garantie du moteur de reference sur ce corpus
        CHECK(a->coverage_ratio > 0.95);
        CHECK(a->overlap_ratio >= a->coverage_ratio - 1e-9);
        CHECK(a->overlap_peak >= 1);
        CHECK(a->min_crossing_mm <= a->mean_crossing_mm);
        CHECK(a->mean_crossing_mm <= a->max_crossing_mm);
        CHECK(a->max_return_mm > 0.0);
    }
}

TEST_CASE("strategy: un manque est localise dans la region", "[satin_strategy]") {
    // deep_channel : les bras larges ne sont pas couverts (limite connue du moteur).
    const auto region = as::make_shape("deep_channel");
    REQUIRE(region.has_value());
    const auto m = as::measure_satin(as::SatinStrategy::Skeleton, *region, {});
    REQUIRE(m.has_value());
    REQUIRE(m->uncovered_area_mm2 > 50.0);
    CHECK(m->uncovered_min.x.value < m->uncovered_max.x.value);
    CHECK(m->uncovered_min.y.value < m->uncovered_max.y.value);
    // La boite des manques reste dans la boite de la region (a un pixel de 100 um pres).
    std::int32_t xmin = INT32_MAX, xmax = INT32_MIN, ymin = INT32_MAX, ymax = INT32_MIN;
    for (const auto& n : region->outer.nodes) {
        xmin = std::min<std::int32_t>(xmin, n.pos.x.value);
        xmax = std::max<std::int32_t>(xmax, n.pos.x.value);
        ymin = std::min<std::int32_t>(ymin, n.pos.y.value);
        ymax = std::max<std::int32_t>(ymax, n.pos.y.value);
    }
    CHECK(m->uncovered_min.x.value >= xmin - 100);
    CHECK(m->uncovered_max.x.value <= xmax + 100);
    CHECK(m->uncovered_min.y.value >= ymin - 100);
    CHECK(m->uncovered_max.y.value <= ymax + 100);
}

TEST_CASE("strategy: sans mesure de couverture les champs de couverture restent nuls",
          "[satin_strategy]") {
    const auto region = as::make_shape("rectangle");
    REQUIRE(region.has_value());
    const auto res = as::generate_satin(as::SatinStrategy::Skeleton, *region, {});
    REQUIRE(res.has_value());
    const auto m = as::measure_result(*res);
    CHECK(m.crossings > 0);
    CHECK(m.coverage_ratio == 0.0);
    CHECK(m.overlap_peak == 0);
    CHECK(m.uncovered_area_mm2 == 0.0);
}
