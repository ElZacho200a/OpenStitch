// SPDX-License-Identifier: Apache-2.0
// Axe médian vectoriel (lot B de docs/auto-satin-research.md) : mêmes traversées finales que le
// squelette raster, mais l'axe vient du diagramme de Voronoï des segments du contour.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include "openstitch/auto_satin/medial_axis_satin.hpp"
#include "openstitch/auto_satin/satin_strategy.hpp"
#include "openstitch/auto_satin/shapes.hpp"

namespace as = openstitch::auto_satin;
using openstitch::Micrometers;
using openstitch::Vec2um;
using openstitch::geometry::Path;
using openstitch::geometry::PathNode;
using openstitch::geometry::PathSet;

namespace {

as::SkeletonSatinParameters medial_params() {
    as::SkeletonSatinParameters p;
    p.strategy = as::SatinStrategy::MedialAxis;
    p.measure_coverage = true;
    return p;
}

as::SkeletonSatinResult medial(const PathSet& region,
                               const as::SkeletonSatinParameters& p = medial_params()) {
    const auto r = as::generate_medial_axis_satin(region, p);
    REQUIRE(r.has_value());
    return *r;
}

PathSet shape(const std::string& name) {
    const auto region = as::make_shape(name);
    REQUIRE(region.has_value());
    return *region;
}

PathSet translated(const PathSet& region, std::int32_t dx, std::int32_t dy) {
    PathSet out = region;
    const auto move = [&](Path& path) {
        for (auto& n : path.nodes) {
            n.pos.x = Micrometers{n.pos.x.value + dx};
            n.pos.y = Micrometers{n.pos.y.value + dy};
        }
    };
    move(out.outer);
    for (auto& h : out.holes) {
        move(h);
    }
    return out;
}

PathSet polygon(const std::vector<std::pair<int, int>>& pts) {
    PathSet set;
    for (const auto& [x, y] : pts) {
        PathNode n;
        n.pos = Vec2um{Micrometers{x}, Micrometers{y}};
        set.outer.nodes.push_back(n);
    }
    return set;
}

int crossing_pairs(const as::SkeletonSatinResult& r) {
    return as::measure_result(r).crossing_pairs;
}

} // namespace

TEST_CASE("medial: couverture et croisements sur le corpus", "[medial_axis]") {
    struct Floor {
        const char* name;
        double coverage;
    };
    // Planchers établis après mesure (2026-10-15), marge ~1 point ; pas des seuils textiles.
    const Floor floors[] = {
        {"rectangle", 0.99},
        {"capsule", 0.99},
        {"ribbon", 0.98},
        {"s", 0.98},
        {"y", 0.98},
        {"t", 0.98},
        {"cross", 0.98},
        {"h", 0.98},
        {"circle", 0.98},
        {"disc_15mm", 0.98},
        {"petal", 0.98},
        {"ring", 0.98},
        {"E", 0.98},
        {"star5", 0.98},
        {"trident", 0.97},
        {"comb", 0.95},
        {"deep_recursive", 0.98},
        {"two_holes", 0.98},
        {"junction_with_hole", 0.98},
        {"polygonal_cut_fixture", 0.97},
        {"deep_channel", 0.90},
    };
    for (const auto& f : floors) {
        INFO(f.name);
        const auto r = medial(shape(f.name));
        CHECK_FALSE(r.columns.empty());
        CHECK(r.diagnostics.coverage_ratio >= f.coverage);
        CHECK(crossing_pairs(r) == 0);
    }
}

TEST_CASE("medial: la facade route la strategie et la forme compacte n'est plus refusee",
          "[medial_axis]") {
    const auto region = shape("circle");
    const auto viaFacade = as::generate_satin(as::SatinStrategy::MedialAxis, region, {});
    const auto direct = as::generate_medial_axis_satin(region, medial_params());
    REQUIRE(viaFacade.has_value());
    REQUIRE(direct.has_value());
    CHECK(viaFacade->columns.size() == direct->columns.size());
    CHECK_FALSE(viaFacade->columns.empty());
}

TEST_CASE("medial: deterministe et invariant par translation", "[medial_axis]") {
    for (const char* name : {"s", "cross", "E", "ring", "two_holes"}) {
        INFO(name);
        const auto region = shape(name);
        const auto a = medial(region);
        const auto b = medial(region);
        REQUIRE(a.columns.size() == b.columns.size());
        for (std::size_t i = 0; i < a.columns.size(); ++i) {
            REQUIRE(a.columns[i].crossings.size() == b.columns[i].crossings.size());
            for (std::size_t k = 0; k < a.columns[i].crossings.size(); ++k) {
                CHECK(a.columns[i].crossings[k].a == b.columns[i].crossings[k].a);
                CHECK(a.columns[i].crossings[k].b == b.columns[i].crossings[k].b);
            }
        }
        // Le diagramme ne dépend pas de l'origine : même nombre de colonnes et de traversées.
        const auto moved = medial(translated(region, 1'234'567, -765'432));
        const auto ma = as::measure_result(a);
        const auto mb = as::measure_result(moved);
        CHECK(ma.columns == mb.columns);
        CHECK(std::abs(ma.crossings - mb.crossings) <= 2);
    }
}

TEST_CASE("medial: un anneau donne une colonne fermee", "[medial_axis]") {
    const auto r = medial(shape("ring"));
    REQUIRE(r.columns.size() == 1);
    const auto& cs = r.columns[0].crossings;
    REQUIRE(cs.size() > 10);
    CHECK(cs.front().a == cs.back().a);
    CHECK(cs.front().b == cs.back().b);
}

TEST_CASE("medial: un contour qui se coupe est refuse par une erreur nommee", "[medial_axis]") {
    // Noeud papillon : deux aretes se croisent franchement.
    const auto bowtie = polygon({{0, 0}, {10'000, 10'000}, {10'000, 0}, {0, 10'000}});
    const auto r = as::generate_medial_axis_satin(bowtie, medial_params());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find("se coupe") != std::string::npos);
}

TEST_CASE("medial: un contour trop complexe est refuse, sans repli silencieux", "[medial_axis]") {
    std::vector<std::pair<int, int>> pts;
    const int n = 7000;
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * std::numbers::pi * i / n;
        pts.push_back({static_cast<int>(std::lround(200'000.0 * std::cos(a))),
                       static_cast<int>(std::lround(200'000.0 * std::sin(a)))});
    }
    const auto r = as::generate_medial_axis_satin(polygon(pts), medial_params());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find("trop complexe") != std::string::npos);
}

TEST_CASE("medial: un guide absolu change l'orientation des traversees", "[medial_axis]") {
    const auto region = shape("capsule");
    // Ancre au centre de la boite de la region, sur l'axe de la capsule.
    std::int64_t sx = 0;
    std::int64_t sy = 0;
    for (const auto& n : region.outer.nodes) {
        sx += n.pos.x.value;
        sy += n.pos.y.value;
    }
    const auto count = static_cast<std::int64_t>(region.outer.nodes.size());
    auto p = medial_params();
    p.guides.push_back({Vec2um{Micrometers{static_cast<std::int32_t>(sx / count)},
                               Micrometers{static_cast<std::int32_t>(sy / count)}},
                        std::numbers::pi / 4.0, true});
    const auto with = medial(region, p);
    const auto without = medial(region);
    REQUIRE_FALSE(with.columns.empty());
    REQUIRE_FALSE(without.columns.empty());
    CHECK(with.diagnostics.orphan_guides == 0);
    const auto mid = [](const as::SkeletonSatinResult& r) {
        const auto& cs = r.columns.front().crossings;
        return cs[cs.size() / 2];
    };
    const auto angle = [](const as::SkeletonSatinCrossing& c) {
        return std::atan2(double(c.b.y.value - c.a.y.value), double(c.b.x.value - c.a.x.value));
    };
    // Sans guide : perpendiculaire a l'axe horizontal (vertical) ; avec guide : 45 degres.
    const double plain = std::abs(std::remainder(angle(mid(without)), std::numbers::pi));
    const double guided = std::abs(std::remainder(angle(mid(with)), std::numbers::pi));
    CHECK(std::abs(plain - std::numbers::pi / 2.0) < 0.1);
    CHECK(std::abs(guided - std::numbers::pi / 4.0) < 0.1);
}
