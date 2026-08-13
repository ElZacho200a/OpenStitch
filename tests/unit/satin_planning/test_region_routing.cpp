// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/satin_planning/region_routing.hpp"
#include "openstitch/satin_planning/region_split.hpp"

using namespace openstitch;
using namespace openstitch::satin_planning;

namespace {

RegionRoutingParams parametric_routing_params() {
    RegionRoutingParams params;
    params.genParams.geometry_mode = auto_satin::SatinGeometryMode::Parametric;
    return params;
}

RegionSplitReport split_shape(const std::string& name, geometry::PathSet& shapeOut) {
    const auto shape = auto_satin::make_shape(name);
    REQUIRE(shape.has_value());
    shapeOut = *shape;
    const auto analysis = auto_satin::analyze_region(*shape, {});
    REQUIRE(analysis.has_value());
    const DecompositionReport decomposition = decompose_into_paths(analysis->debug.graph);
    return split_region(*shape, analysis->debug.graph, decomposition);
}

}  // namespace

TEST_CASE("route_regions : rectangle -- une seule region, un seul pas de depart") {
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("rectangle", shape);
    REQUIRE(split.regions.size() == 1);

    const RegionRoutingReport report = route_regions(split, parametric_routing_params());
    REQUIRE(report.regions.size() == 1);
    CHECK(report.regions.front().build_succeeded);
    REQUIRE(report.plan.steps.size() == 1);
    CHECK(report.plan.steps.front().connector == stitch_generation::ConnectorKind::Start);
    CHECK(report.plan.jumps == 0);
}

TEST_CASE("route_regions : t -- route les deux regions, plan coherent avec le nombre de colonnes construites") {
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("t", shape);
    REQUIRE(split.regions.size() == 2);

    const RegionRoutingReport report = route_regions(split, parametric_routing_params());
    REQUIRE(report.regions.size() == 2);
    const std::size_t builtCount =
        static_cast<std::size_t>(std::count_if(report.regions.begin(), report.regions.end(),
                                                [](const RoutedRegion& r) { return r.build_succeeded; }));
    CHECK(builtCount == 2);
    REQUIRE(report.plan.steps.size() == builtCount);

    // Exactement un pas de depart, le reste des liaisons (underpath ou jump).
    const std::size_t startCount =
        static_cast<std::size_t>(std::count_if(report.plan.steps.begin(), report.plan.steps.end(), [](const auto& s) {
            return s.connector == stitch_generation::ConnectorKind::Start;
        }));
    CHECK(startCount == 1);
    CHECK(report.plan.jumps + report.plan.underpaths == builtCount - 1);

    // Chaque chemin de la decomposition est represente exactement une fois
    // dans le plan (aucune region omise, aucune dupliquee).
    std::vector<bool> seen(split.regions.size(), false);
    for (const auto& step : report.plan.steps) {
        REQUIRE(step.column_index < report.regions.size());
        CHECK_FALSE(seen[step.column_index]);
        seen[step.column_index] = true;
    }
}

TEST_CASE("format_region_routing_report : rendu textuel exploitable pour le debug") {
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("t", shape);
    const RegionRoutingReport report = route_regions(split, parametric_routing_params());
    const std::string text = format_region_routing_report(report);
    CHECK_FALSE(text.empty());
    CHECK(text.find("Total :") != std::string::npos);
    CHECK(text.find("Plan :") != std::string::npos);
}
