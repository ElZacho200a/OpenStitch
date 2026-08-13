// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/satin_coverage/coverage.hpp"
#include "openstitch/satin_planning/region_split.hpp"

namespace openstitch::satin_planning {

// Verdict complet d'une SatinRegion (phase 3) passee par le VRAI generateur
// (auto_satin::build_satin_columns) puis mesuree par le Coverage Analyzer,
// qui reste totalement INDEPENDANT du decomposeur (spec SGSD §15) --
// `satin_coverage` ne connait que des rails/barreaux, jamais un type
// `auto_satin` ou `satin_planning`. Contrairement a la phase 4 (qui ne
// verifie que le statut de satinabilite AVANT generation), cet oracle
// mesure le resultat REEL une fois les rails et barreaux effectivement
// construits -- « la preuve finale reste : est-ce que l'Auto-Satin genere
// couvre reellement correctement la region ? ».
struct RegionGenerationVerdict {
    std::size_t path_index{0};
    bool build_succeeded{false};
    std::string build_refusal;  // rempli si build_succeeded == false (refus ou echec de couverture)
    std::optional<satin_coverage::SatinCoverageReport> coverage;
    bool passed{false};  // == coverage->passed ; toujours false si build_succeeded == false
};

// Construit reellement les colonnes satin sur `region.region` avec
// `genParams` (mode `Legacy` par defaut, comme `SatinColumnsParameters`
// elle-meme -- passer `geometry_mode = Parametric` explicitement pour
// reproduire le comportement de production d'`autodigitize.cpp`), puis
// mesure la couverture obtenue avec `satin_coverage::analyze_satin_coverage`
// (reutilise `parametric_columns` si non vide, sinon `columns` -- meme
// selection que `autodigitize.cpp`/le CLI). Ne modifie jamais `region`.
[[nodiscard]] RegionGenerationVerdict evaluate_region_generation(
    const SatinRegion& region, const auto_satin::SatinColumnsParameters& genParams = {},
    const satin_coverage::SatinCoverageConfig& coverageConfig = {}, Micrometers density = Micrometers{400});

struct DecompositionGenerationReport {
    std::vector<RegionGenerationVerdict> verdicts;  // une par SatinRegion isolee a la phase 3
    std::vector<std::size_t> failed;                // path_index : verdict negatif OU chemin jamais isole
    // Couverture agregee (aire couverte / aire cible) sur toutes les
    // regions ayant produit un rapport de couverture -- indicateur global
    // de qualite du decoupage complet, pas seulement region par region.
    double aggregate_coverage_ratio{0.0};
};

// Applique `evaluate_region_generation` a chaque region de `split`, et
// reporte aussi automatiquement dans `failed` les chemins que la phase 3
// n'a jamais reussi a isoler (`split.unresolved_paths`).
[[nodiscard]] DecompositionGenerationReport evaluate_decomposition_generation(
    const RegionSplitReport& split, const auto_satin::SatinColumnsParameters& genParams = {},
    const satin_coverage::SatinCoverageConfig& coverageConfig = {}, Micrometers density = Micrometers{400});

[[nodiscard]] std::string format_generation_report(const DecompositionGenerationReport& report);

}  // namespace openstitch::satin_planning
