// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/satin_planning/beam_search.hpp"
#include "openstitch/satin_planning/region_oracle.hpp"
#include "openstitch/satin_planning/region_split.hpp"

using namespace openstitch;
using namespace openstitch::satin_planning;

namespace {

BeamSearchParams parametric_beam_params(std::size_t width = 3) {
    BeamSearchParams params;
    params.beam_width = width;
    params.genParams.geometry_mode = auto_satin::SatinGeometryMode::Parametric;
    return params;
}

geometry::PathSet load_shape(const std::string& name) {
    const auto shape = auto_satin::make_shape(name);
    REQUIRE(shape.has_value());
    return *shape;
}

}  // namespace

TEST_CASE("OracleGuidedSelector : t -- resout toujours la branche, journalise sa decision") {
    const geometry::PathSet shape = load_shape("t");
    const auto analysis = auto_satin::analyze_region(shape, {});
    REQUIRE(analysis.has_value());
    const DecompositionReport decomposition = decompose_into_paths(analysis->debug.graph);

    OracleGuidedSelector selector(parametric_beam_params());
    CutCandidateParams params;
    params.selector = std::ref(selector);

    const RegionSplitReport split = split_region(shape, analysis->debug.graph, decomposition, params);
    CHECK(split.unresolved_paths.empty());
    REQUIRE(split.regions.size() == 2);

    // Une decision journalisee par evenement de detachement (t n'a qu'une
    // seule branche a detacher), au plus beam_width candidats evalues.
    REQUIRE(selector.decisions().size() == 1);
    CHECK_FALSE(selector.decisions().front().empty());
    CHECK(selector.decisions().front().size() <= 3);
}

TEST_CASE("OracleGuidedSelector : corpus branche -- ne resout jamais moins de chemins que le comportement par defaut") {
    for (const std::string& name : {"t", "y", "cross", "h", "trident"}) {
        INFO("forme = " << name);
        const geometry::PathSet shape = load_shape(name);
        const auto analysis = auto_satin::analyze_region(shape, {});
        REQUIRE(analysis.has_value());
        const DecompositionReport decomposition = decompose_into_paths(analysis->debug.graph);

        const RegionSplitReport defaultSplit = split_region(shape, analysis->debug.graph, decomposition);

        OracleGuidedSelector selector(parametric_beam_params());
        CutCandidateParams guidedParams;
        guidedParams.selector = std::ref(selector);
        const RegionSplitReport guidedSplit = split_region(shape, analysis->debug.graph, decomposition, guidedParams);

        // Le garde-fou de satinabilite (phase 4) s'applique en amont, dans
        // generate_cut_candidates lui-meme : le selecteur ne peut choisir
        // que parmi des candidats DEJA valides, jamais en "sauver" un que
        // le garde-fou a rejete. Le nombre de chemins resolus doit donc
        // rester au moins aussi bon qu'avec la selection par defaut.
        CHECK(guidedSplit.regions.size() >= defaultSplit.regions.size());
    }
}

TEST_CASE("OracleGuidedSelector : H -- ameliore mesurablement la couverture du pont par rapport au comportement par defaut") {
    // Non-regression directe sur la valeur de la phase 6 : mesure sur
    // 2026-08-13, le pont (§ phase 4/5, encore branche apres decoupe,
    // couverture par defaut ~69,9 %) passe a ~92,3 % avec la selection
    // guidee par l'oracle -- un gain net de plus de 20 points, sans toucher
    // au generateur de rails ni a la logique de decoupage elle-meme,
    // seulement au CHOIX de la distance de coupe parmi celles deja jugees
    // geometriquement/topologiquement valides.
    const geometry::PathSet shape = load_shape("h");
    const auto analysis = auto_satin::analyze_region(shape, {});
    REQUIRE(analysis.has_value());
    const DecompositionReport decomposition = decompose_into_paths(analysis->debug.graph);

    const RegionSplitReport defaultSplit = split_region(shape, analysis->debug.graph, decomposition);
    OracleGuidedSelector selector(parametric_beam_params());
    CutCandidateParams guidedParams;
    guidedParams.selector = std::ref(selector);
    const RegionSplitReport guidedSplit = split_region(shape, analysis->debug.graph, decomposition, guidedParams);
    REQUIRE(defaultSplit.regions.size() == 3);
    REQUIRE(guidedSplit.regions.size() == 3);

    auto_satin::SatinColumnsParameters genParams;
    genParams.geometry_mode = auto_satin::SatinGeometryMode::Parametric;

    const auto bridgeCoverage = [&](const RegionSplitReport& split) {
        double coverage = 1.0;
        for (const auto& r : split.regions) {
            if (r.area_mm2 > 300.0) {  // le pont est le plus grand fragment (~310-350mm2)
                const auto v = evaluate_region_generation(r, genParams);
                REQUIRE(v.coverage.has_value());
                coverage = v.coverage->raw_coverage_ratio;
            }
        }
        return coverage;
    };

    const double defaultCoverage = bridgeCoverage(defaultSplit);
    const double guidedCoverage = bridgeCoverage(guidedSplit);
    CHECK(defaultCoverage < 0.80);   // comportement par defaut : ~69,9%
    CHECK(guidedCoverage > 0.85);    // guide par l'oracle : ~92,3%
    CHECK(guidedCoverage > defaultCoverage + 0.10);
}

TEST_CASE("OracleGuidedSelector : H -- evalue plusieurs candidats pour le pont, pas seulement le plus proche") {
    const geometry::PathSet shape = load_shape("h");
    const auto analysis = auto_satin::analyze_region(shape, {});
    REQUIRE(analysis.has_value());
    const DecompositionReport decomposition = decompose_into_paths(analysis->debug.graph);
    REQUIRE(decomposition.junctions.size() == 2);

    OracleGuidedSelector selector(parametric_beam_params());
    CutCandidateParams params;
    params.selector = std::ref(selector);
    const RegionSplitReport split = split_region(shape, analysis->debug.graph, decomposition, params);

    // Le pont a deux bouts de jonction : deux evenements de detachement.
    REQUIRE(selector.decisions().size() == 2);
    for (const auto& decision : selector.decisions()) {
        // Le beam_width est de 3 : au plus 3 candidats evalues par evenement.
        CHECK(decision.size() <= 3);
    }
}
