// SPDX-License-Identifier: Apache-2.0
#include "openstitch/satin_planning/region_satinability.hpp"

#include <sstream>

namespace openstitch::satin_planning {

namespace {

bool is_ready(auto_satin::SatinabilityStatus status) {
    return status == auto_satin::SatinabilityStatus::Suitable ||
           status == auto_satin::SatinabilityStatus::SuitableWithWarnings;
}

}  // namespace

RegionSatinabilityVerdict check_region_satinability(const SatinRegion& region,
                                                      const auto_satin::AutoSatinParameters& params) {
    RegionSatinabilityVerdict verdict;
    verdict.path_index = region.path_index;

    const auto analysis = auto_satin::analyze_region(region.region, params);
    if (!analysis.has_value()) {
        verdict.ready_for_generation = false;
        verdict.note = "echec de l'analyse de satinabilite : " + analysis.error().message;
        return verdict;
    }

    verdict.report = analysis->report;
    verdict.ready_for_generation = is_ready(analysis->report.status);
    return verdict;
}

DecompositionSatinabilityReport check_all_regions(const RegionSplitReport& split,
                                                    const auto_satin::AutoSatinParameters& params) {
    DecompositionSatinabilityReport out;
    for (const auto& region : split.regions) {
        RegionSatinabilityVerdict verdict = check_region_satinability(region, params);
        if (!verdict.ready_for_generation) out.not_ready.push_back(verdict.path_index);
        out.verdicts.push_back(std::move(verdict));
    }
    for (auto pathIndex : split.unresolved_paths) {
        RegionSatinabilityVerdict verdict;
        verdict.path_index = pathIndex;
        verdict.ready_for_generation = false;
        verdict.note = "chemin jamais isole a la phase 3 (aucune coupe valide trouvee)";
        out.not_ready.push_back(pathIndex);
        out.verdicts.push_back(std::move(verdict));
    }
    return out;
}

std::string format_satinability_report(const DecompositionSatinabilityReport& report) {
    std::ostringstream out;
    out << "[SGSD phase 4 -- satinabilite des regions decoupees]\n\n";
    for (const auto& v : report.verdicts) {
        out << "chemin " << v.path_index << " : ";
        if (v.report.has_value()) {
            out << auto_satin::to_string(v.report->status) << " (jonctions=" << v.report->junction_count
                << ", extremites=" << v.report->endpoint_count
                << ", variation_largeur=" << v.report->width_variation << ")";
        } else {
            out << "pas de region (" << v.note << ")";
        }
        out << (v.ready_for_generation ? "  [PRET]" : "  [PAS PRET]") << "\n";
        if (v.report.has_value()) {
            for (const auto& issue : v.report->issues) out << "        - " << issue.message << "\n";
        }
    }
    out << "\nTotal : " << (report.verdicts.size() - report.not_ready.size()) << "/" << report.verdicts.size()
        << " region(s) prete(s) pour la generation Auto-Satin\n";
    return out.str();
}

}  // namespace openstitch::satin_planning
