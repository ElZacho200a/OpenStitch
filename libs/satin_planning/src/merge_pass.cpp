// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/merge_pass.hpp"

#include <algorithm>
#include <sstream>

namespace openstitch::satin_planning {

namespace {

const SatinRegion* find_region(const RegionSplitReport& split, std::size_t pathIndex) {
    for (const auto& r : split.regions) {
        if (r.path_index == pathIndex) return &r;
    }
    return nullptr;
}

}  // namespace

MergePassReport evaluate_merge_pass(const RegionSplitReport& split, const MergePassParams& params) {
    MergePassReport report;

    for (const auto& candidate : split.merge_candidates) {
        const SatinRegion* first = find_region(split, candidate.first_path_index);
        const SatinRegion* second = find_region(split, candidate.second_path_index);
        if (first == nullptr || second == nullptr) continue;  // deja filtre par split_region, garde-fou

        MergeDecision decision;
        decision.first_path_index = candidate.first_path_index;
        decision.second_path_index = candidate.second_path_index;

        const RegionGenerationVerdict firstVerdict =
            evaluate_region_generation(*first, params.genParams, params.coverageConfig, params.density);
        const RegionGenerationVerdict secondVerdict =
            evaluate_region_generation(*second, params.genParams, params.coverageConfig, params.density);
        const double firstCoverage = firstVerdict.coverage ? firstVerdict.coverage->raw_coverage_ratio : 0.0;
        const double secondCoverage = secondVerdict.coverage ? secondVerdict.coverage->raw_coverage_ratio : 0.0;
        const double totalArea = first->area_mm2 + second->area_mm2;
        decision.separate_coverage_ratio =
            totalArea > 0.0 ? (firstCoverage * first->area_mm2 + secondCoverage * second->area_mm2) / totalArea : 0.0;

        SatinRegion merged;
        merged.path_index = candidate.first_path_index;  // convention : porte l'identite du premier des deux
        merged.region = candidate.merged_region;
        merged.area_mm2 = candidate.merged_area_mm2;
        const RegionGenerationVerdict mergedVerdict =
            evaluate_region_generation(merged, params.genParams, params.coverageConfig, params.density);
        decision.merged_build_succeeded = mergedVerdict.build_succeeded;
        decision.merged_coverage_ratio = mergedVerdict.coverage ? mergedVerdict.coverage->raw_coverage_ratio : 0.0;

        decision.merge_recommended = decision.merged_build_succeeded &&
                                      decision.merged_coverage_ratio >=
                                          decision.separate_coverage_ratio - params.coverage_tolerance;

        report.decisions.push_back(std::move(decision));
    }

    return report;
}

std::string format_merge_pass_report(const MergePassReport& report) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);

    out << "[SGSD phase 7 -- passe de fusion]\n\n";
    for (const auto& d : report.decisions) {
        out << "chemins " << d.first_path_index << "+" << d.second_path_index << " : separe="
            << (d.separate_coverage_ratio * 100.0) << "% fusionne="
            << (d.merged_build_succeeded ? std::to_string(d.merged_coverage_ratio * 100.0) + "%" : std::string("echec"))
            << (d.merge_recommended ? "  [FUSIONNER]" : "  [GARDER SEPARE]") << "\n";
    }
    const auto recommended =
        static_cast<std::size_t>(std::count_if(report.decisions.begin(), report.decisions.end(),
                                                [](const MergeDecision& d) { return d.merge_recommended; }));
    out << "\nTotal : " << recommended << "/" << report.decisions.size() << " fusion(s) recommandee(s)\n";
    return out.str();
}

}  // namespace openstitch::satin_planning
