// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/satin_planning/overlap.hpp"
#include "openstitch/satin_planning/region_split.hpp"

using namespace openstitch;
using namespace openstitch::satin_planning;

namespace {

RegionSplitReport split_shape(const std::string& name, geometry::PathSet& shapeOut) {
    const auto shape = auto_satin::make_shape(name);
    REQUIRE(shape.has_value());
    shapeOut = *shape;
    const auto analysis = auto_satin::analyze_region(*shape, {});
    REQUIRE(analysis.has_value());
    const DecompositionReport decomposition = decompose_into_paths(analysis->debug.graph);
    return split_region(*shape, analysis->debug.graph, decomposition);
}

const SatinRegion* find_region(const RegionSplitReport& split, std::size_t pathIndex) {
    for (const auto& r : split.regions) {
        if (r.path_index == pathIndex) return &r;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("generate_overlaps : t -- ferme reellement l'interstice de coupe") {
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("t", shape);
    REQUIRE(split.merge_candidates.size() == 1);

    const OverlapReport report = generate_overlaps(split);
    REQUIRE(report.overlaps.size() == 1);
    const auto& overlap = report.overlaps.front();

    const auto& candidate = split.merge_candidates.front();
    const SatinRegion* first = find_region(split, candidate.first_path_index);
    const SatinRegion* second = find_region(split, candidate.second_path_index);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    // Chaque region elargie est strictement plus grande que l'originale...
    CHECK(overlap.first_extended_area_mm2 > first->area_mm2);
    CHECK(overlap.second_extended_area_mm2 > second->area_mm2);
    // ... mais jamais au-dela de la geometrie exacte d'avant coupe (§19 :
    // "rester dans la shape originale").
    CHECK(overlap.first_extended_area_mm2 <= candidate.merged_area_mm2 + 0.01);
    CHECK(overlap.second_extended_area_mm2 <= candidate.merged_area_mm2 + 0.01);

    // Preuve du point central de la phase : les deux regions elargies se
    // chevauchent maintenant reellement (l'interstice de coupe est ferme).
    const auto intersection =
        geometry::intersect_polygons({overlap.first_extended}, {overlap.second_extended});
    REQUIRE(intersection.has_value());
    REQUIRE_FALSE(intersection->empty());
    double intersectionAreaMm2 = 0.0;
    for (const auto& piece : *intersection) intersectionAreaMm2 += geometry::path_set_area_um2(piece) / 1e6;
    CHECK(intersectionAreaMm2 > 0.0);
}

TEST_CASE("generate_overlaps : cross -- un recouvrement par paire directement adjacente") {
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("cross", shape);
    REQUIRE(split.merge_candidates.size() == 1);

    const OverlapReport report = generate_overlaps(split);
    CHECK(report.overlaps.size() == split.merge_candidates.size());
}

TEST_CASE("format_overlap_report : rendu textuel exploitable pour le debug") {
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("t", shape);
    const OverlapReport report = generate_overlaps(split);
    const std::string text = format_overlap_report(report);
    CHECK_FALSE(text.empty());
    CHECK(text.find("Total :") != std::string::npos);
}
