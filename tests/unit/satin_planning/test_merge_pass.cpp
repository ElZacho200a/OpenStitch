// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/satin_planning/merge_pass.hpp"
#include "openstitch/satin_planning/region_split.hpp"

using namespace openstitch;
using namespace openstitch::satin_planning;

namespace {

MergePassParams parametric_merge_params() {
    MergePassParams params;
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

TEST_CASE("split_region : t -- expose exactement un candidat de fusion coherent") {
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("t", shape);
    REQUIRE(split.regions.size() == 2);
    REQUIRE(split.merge_candidates.size() == 1);

    const auto& candidate = split.merge_candidates.front();
    CHECK(candidate.first_path_index != candidate.second_path_index);
    const double originalAreaMm2 = geometry::path_set_area_um2(shape) / 1e6;
    // La geometrie fusionnee est EXACTE (pas de reconstruction Clipper2) :
    // son aire doit correspondre a l'aire d'origine (aucune coupe appliquee
    // dans cette geometrie-la).
    CHECK(candidate.merged_area_mm2 == Catch::Approx(originalAreaMm2).margin(0.01));
}

TEST_CASE("split_region : cross -- seule la paire de feuilles adjacentes devient un candidat") {
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("cross", shape);
    REQUIRE(split.regions.size() == 3);
    // Sur les deux coupes de `cross` (degre 4 : une continuation principale
    // + deux branches detachees a la meme jonction), une seule laisse ses
    // DEUX enfants comme feuilles finales -- l'autre a son "reste" redecoupe
    // par l'evenement suivant (§ commentaire MergeCandidate).
    REQUIRE(split.merge_candidates.size() == 1);
}

TEST_CASE("evaluate_merge_pass : t -- decision exploitable dans les deux sens") {
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("t", shape);
    REQUIRE(split.merge_candidates.size() == 1);

    const MergePassReport report = evaluate_merge_pass(split, parametric_merge_params());
    INFO(format_merge_pass_report(report));
    REQUIRE(report.decisions.size() == 1);
    const auto& decision = report.decisions.front();
    CHECK(decision.separate_coverage_ratio > 0.0);
    // Ne prejuge pas du sens de la recommandation (dependant de la
    // geometrie reelle) : verifie seulement que la mesure est coherente.
    if (decision.merged_build_succeeded) {
        CHECK(decision.merged_coverage_ratio > 0.0);
    } else {
        CHECK_FALSE(decision.merge_recommended);
    }
}

TEST_CASE("format_merge_pass_report : rendu textuel exploitable pour le debug") {
    geometry::PathSet shape;
    const RegionSplitReport split = split_shape("t", shape);
    const MergePassReport report = evaluate_merge_pass(split, parametric_merge_params());
    const std::string text = format_merge_pass_report(report);
    CHECK_FALSE(text.empty());
    CHECK(text.find("Total :") != std::string::npos);
}
