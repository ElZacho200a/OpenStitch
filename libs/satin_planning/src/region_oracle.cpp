// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/region_oracle.hpp"

#include <sstream>

namespace openstitch::satin_planning {

namespace {

template <typename Column>
satin_coverage::SatinColumnInput to_coverage_input(const Column& col, Micrometers density) {
    satin_coverage::SatinColumnInput in;
    in.rail_a = col.rail_a;
    in.rail_b = col.rail_b;
    in.rungs.reserve(col.rungs.size());
    for (const auto& r : col.rungs) in.rungs.emplace_back(r.a, r.b);
    in.density = density;
    return in;
}

}  // namespace

RegionGenerationVerdict evaluate_region_generation(const SatinRegion& region,
                                                    const auto_satin::SatinColumnsParameters& genParams,
                                                    const satin_coverage::SatinCoverageConfig& coverageConfig,
                                                    Micrometers density) {
    RegionGenerationVerdict verdict;
    verdict.path_index = region.path_index;

    const auto built = auto_satin::build_satin_columns(region.region, genParams);
    if (!built.refusal.empty()) {
        verdict.build_refusal = built.refusal;
        return verdict;
    }
    if (built.columns.empty() && built.parametric_columns.empty()) {
        verdict.build_refusal =
            std::string("aucune colonne produite (statut ") + auto_satin::to_string(built.status) + ")";
        return verdict;
    }
    verdict.build_succeeded = true;

    std::vector<satin_coverage::SatinColumnInput> inputs;
    if (!built.parametric_columns.empty()) {
        inputs.reserve(built.parametric_columns.size());
        for (const auto& col : built.parametric_columns) inputs.push_back(to_coverage_input(col, density));
    } else {
        inputs.reserve(built.columns.size());
        for (const auto& col : built.columns) inputs.push_back(to_coverage_input(col, density));
    }

    const auto coverage = satin_coverage::analyze_satin_coverage(region.region, inputs, coverageConfig);
    if (!coverage.has_value()) {
        verdict.build_succeeded = false;
        verdict.build_refusal = "echec de l'analyse de couverture : " + coverage.error().message;
        return verdict;
    }
    verdict.coverage = *coverage;
    verdict.passed = coverage->passed;
    return verdict;
}

DecompositionGenerationReport evaluate_decomposition_generation(const RegionSplitReport& split,
                                                                  const auto_satin::SatinColumnsParameters& genParams,
                                                                  const satin_coverage::SatinCoverageConfig& coverageConfig,
                                                                  Micrometers density) {
    DecompositionGenerationReport out;
    double totalTargetMm2 = 0.0;
    double totalCoveredMm2 = 0.0;

    for (const auto& region : split.regions) {
        RegionGenerationVerdict verdict = evaluate_region_generation(region, genParams, coverageConfig, density);
        if (!verdict.passed) out.failed.push_back(verdict.path_index);
        if (verdict.coverage) {
            totalTargetMm2 += verdict.coverage->target_area_mm2;
            totalCoveredMm2 += verdict.coverage->covered_area_mm2;
        }
        out.verdicts.push_back(std::move(verdict));
    }
    for (auto pathIndex : split.unresolved_paths) {
        RegionGenerationVerdict verdict;
        verdict.path_index = pathIndex;
        verdict.build_refusal = "chemin jamais isole a la phase 3 (aucune coupe valide trouvee)";
        out.failed.push_back(pathIndex);
        out.verdicts.push_back(std::move(verdict));
    }

    out.aggregate_coverage_ratio = totalTargetMm2 > 0.0 ? totalCoveredMm2 / totalTargetMm2 * 100.0 : 0.0;
    return out;
}

std::string format_generation_report(const DecompositionGenerationReport& report) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);

    out << "[SGSD phase 5 -- generation + couverture par region]\n\n";
    for (const auto& v : report.verdicts) {
        out << "chemin " << v.path_index << " : ";
        if (v.coverage) {
            out << "cible=" << v.coverage->target_area_mm2 << "mm2 couverte=" << v.coverage->covered_area_mm2
                << "mm2 (" << (v.coverage->raw_coverage_ratio * 100.0) << "% brut, "
                << (v.coverage->core_coverage_ratio * 100.0) << "% coeur)";
        } else {
            out << "echec (" << v.build_refusal << ")";
        }
        out << (v.passed ? "  [PASS]" : "  [FAIL]") << "\n";
    }
    out << "\nCouverture agregee : " << report.aggregate_coverage_ratio << "%\n";
    out << "Total : " << (report.verdicts.size() - report.failed.size()) << "/" << report.verdicts.size()
        << " region(s) reussie(s)\n";
    return out.str();
}

}  // namespace openstitch::satin_planning
