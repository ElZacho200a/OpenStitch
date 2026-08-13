// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "openstitch/satin_planning/region_oracle.hpp"
#include "openstitch/satin_planning/region_split.hpp"

namespace openstitch::satin_planning {

// Parametres de la passe de fusion (phase 7 du plan SGSD, §18 : "le plus
// PETIT nombre de segments permettant une couverture et un satin
// corrects").
struct MergePassParams {
    auto_satin::SatinColumnsParameters genParams{};
    satin_coverage::SatinCoverageConfig coverageConfig{};
    Micrometers density{400};
    // Tolerance de degradation acceptee pour prefer un seul segment a deux :
    // fusionne si couverture_fusionnee >= couverture_separee - tolerance,
    // meme si legerement inferieure -- un seul segment vaut une petite perte
    // de couverture, l'inverse (fusionner malgre une degradation IMPORTANTE)
    // non.
    double coverage_tolerance{0.02};
};

// Verdict d'un candidat de fusion (`RegionSplitReport::merge_candidates`) :
// couverture des deux regions separees (moyenne ponderee par aire) contre
// couverture de la region fusionnee, et la recommandation qui en decoule.
struct MergeDecision {
    std::size_t first_path_index{0};
    std::size_t second_path_index{0};
    double separate_coverage_ratio{0.0};  // moyenne ponderee par aire de evaluate_region_generation(first)/(second)
    double merged_coverage_ratio{0.0};    // 0 si la construction fusionnee echoue
    bool merged_build_succeeded{false};
    bool merge_recommended{false};
};

struct MergePassReport {
    std::vector<MergeDecision> decisions;  // une par MergeCandidate de RegionSplitReport
};

// Evalue CHAQUE candidat de fusion independamment (pas de fusion en cascade
// -- une region fusionnee n'est jamais elle-meme retestee contre un
// troisieme voisin dans cette phase, §16/§18 spec SGSD : eviter l'explosion
// combinatoire ; une passe iterative "jusqu'a stabilite" reste un travail
// futur) : construit reellement les deux regions separees ET la region
// fusionnee (auto_satin::build_satin_columns + satin_coverage, l'oracle
// complet de la phase 5) et recommande la fusion si la couverture obtenue
// ne degrade pas de plus de `coverage_tolerance` par rapport a la moyenne
// ponderee des deux regions separees.
[[nodiscard]] MergePassReport evaluate_merge_pass(const RegionSplitReport& split, const MergePassParams& params = {});

[[nodiscard]] std::string format_merge_pass_report(const MergePassReport& report);

}  // namespace openstitch::satin_planning
