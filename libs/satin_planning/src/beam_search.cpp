// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/beam_search.hpp"

#include <cmath>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/cut.hpp"

namespace openstitch::satin_planning {

OracleGuidedSelector::OracleGuidedSelector(BeamSearchParams params) : params_(std::move(params)) {}

std::optional<std::size_t> OracleGuidedSelector::operator()(const geometry::PathSet& piece,
                                                              const std::vector<CutCandidate>& candidates) {
    std::vector<BeamCandidateScore> scores;
    std::optional<std::size_t> bestIndex;
    double bestCoverage = -1.0;

    for (std::size_t i = 0; i < candidates.size() && scores.size() < params_.beam_width; ++i) {
        const CutCandidate& cand = candidates[i];
        if (!cand.valid) continue;

        BeamCandidateScore score;
        score.candidate_index = i;

        const auto cutResult = geometry::cut_path_set(piece, cand.a, cand.b, params_.cut_width);
        if (cutResult.has_value() && cutResult->size() == 2) {
            // Identifie lequel des deux morceaux est la branche : celui dont
            // l'aire est la plus proche de `branch_piece_area_mm2`, deja
            // calculee correctement par `generate_cut_candidates` (via le
            // noeud distal de l'arete, information que ce selecteur n'a pas
            // besoin de reobtenir ici).
            const double area0 = geometry::path_set_area_um2((*cutResult)[0]) / 1e6;
            const double area1 = geometry::path_set_area_um2((*cutResult)[1]) / 1e6;
            const std::size_t branchIdx =
                std::abs(area0 - cand.branch_piece_area_mm2) <= std::abs(area1 - cand.branch_piece_area_mm2) ? 0 : 1;

            SatinRegion probe;
            probe.region = (*cutResult)[branchIdx];
            probe.area_mm2 = geometry::path_set_area_um2(probe.region) / 1e6;

            const RegionGenerationVerdict verdict =
                evaluate_region_generation(probe, params_.genParams, params_.coverageConfig, params_.density);
            score.build_succeeded = verdict.build_succeeded;
            score.raw_coverage_ratio = verdict.coverage ? verdict.coverage->raw_coverage_ratio : 0.0;
        }

        if (score.build_succeeded && score.raw_coverage_ratio > bestCoverage) {
            bestCoverage = score.raw_coverage_ratio;
            bestIndex = i;
        }
        scores.push_back(score);
    }

    decisions_.push_back(std::move(scores));
    return bestIndex;
}

}  // namespace openstitch::satin_planning
