// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/satin_planning/region_oracle.hpp"
#include "openstitch/satin_planning/region_split.hpp"

using namespace openstitch;
using namespace openstitch::satin_planning;

namespace {

auto_satin::SatinColumnsParameters parametric_params() {
    auto_satin::SatinColumnsParameters params;
    params.geometry_mode = auto_satin::SatinGeometryMode::Parametric;  // comportement de production (autodigitize.cpp)
    return params;
}

RegionSplitReport split_shape(const std::string& shapeName, geometry::PathSet& shapeOut) {
    const auto shape = auto_satin::make_shape(shapeName);
    REQUIRE(shape.has_value());
    shapeOut = *shape;
    const auto analysis = auto_satin::analyze_region(*shape, {});
    REQUIRE(analysis.has_value());
    const DecompositionReport decomposition = decompose_into_paths(analysis->debug.graph);
    return split_region(*shape, analysis->debug.graph, decomposition);
}

}  // namespace

TEST_CASE("evaluate_region_generation : rectangle -- couvre bien sa propre region") {
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("rectangle", shape);
    REQUIRE(split.regions.size() == 1);

    const RegionGenerationVerdict verdict = evaluate_region_generation(split.regions.front(), parametric_params());
    REQUIRE(verdict.build_succeeded);
    REQUIRE(verdict.coverage.has_value());
    CHECK(verdict.coverage->raw_coverage_ratio > 0.90);
}

TEST_CASE("evaluate_decomposition_generation : boucle complete sur le corpus branche") {
    for (const std::string& name : {"t", "y", "cross", "h", "trident"}) {
        INFO("forme = " << name);
        geometry::PathSet shape;
        const RegionSplitReport split = split_shape(name, shape);

        const DecompositionGenerationReport report = evaluate_decomposition_generation(split, parametric_params());
        INFO(format_generation_report(report));

        REQUIRE(report.verdicts.size() == split.regions.size() + split.unresolved_paths.size());
        // Chaque region qui a reellement ete construite doit produire un
        // rapport de couverture exploitable (meme si `passed` est faux --
        // c'est justement ce que cet oracle est cense mesurer).
        for (const auto& v : report.verdicts) {
            if (v.build_succeeded) {
                CHECK(v.coverage.has_value());
                CHECK(v.coverage->target_area_mm2 > 0.0);
            }
        }
    }
}

TEST_CASE("evaluate_decomposition_generation : H -- le pont degrade nettement plus que les montants") {
    // Confirme AVEC DES CHIFFRES reels ce que la phase 4 flaggait deja
    // qualitativement (le pont, encore branche apres decoupe, reste
    // "not_ready") : une fois reellement genere, sa couverture chute
    // nettement par rapport aux montants (jamais coupes qu'a une seule
    // extremite chacun, propres des la phase 4).
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("h", shape);
    const DecompositionGenerationReport report = evaluate_decomposition_generation(split, parametric_params());
    INFO(format_generation_report(report));
    REQUIRE(report.verdicts.size() == 3);

    double bestVertical = 0.0;
    double bridgeCoverage = 1.0;
    for (const auto& v : report.verdicts) {
        REQUIRE(v.coverage.has_value());
        if (v.coverage->target_area_mm2 > 300.0) {  // le pont est le plus grand fragment (~350mm2)
            bridgeCoverage = v.coverage->raw_coverage_ratio;
        } else {
            bestVertical = std::max(bestVertical, v.coverage->raw_coverage_ratio);
        }
    }
    CHECK(bestVertical > 0.95);
    CHECK(bridgeCoverage < 0.85);
}

TEST_CASE("format_generation_report : rendu textuel exploitable pour le debug") {
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("t", shape);
    const DecompositionGenerationReport report = evaluate_decomposition_generation(split, parametric_params());
    const std::string text = format_generation_report(report);
    CHECK_FALSE(text.empty());
    CHECK(text.find("Couverture agregee") != std::string::npos);
    CHECK(text.find("Total :") != std::string::npos);
}
