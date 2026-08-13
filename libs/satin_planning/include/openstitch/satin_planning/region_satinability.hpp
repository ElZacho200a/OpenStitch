// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/satin_planning/region_split.hpp"

namespace openstitch::satin_planning {

// Verdict de satinabilite d'une seule SatinRegion (phase 3). Ne reimplemente
// aucune heuristique : fait tourner le VRAI pipeline existant
// (`auto_satin::analyze_region` -- rasterisation -> distance -> squelette ->
// graphe -> `evaluate_satinability`) sur la region deja decoupee, exactement
// comme le ferait `build_satin_columns` en interne. C'est la preuve requise
// par la spec SGSD (§14) : « la preuve finale reste, est-ce que l'Auto-Satin
// genere couvre reellement correctement la region ? » -- ici son prealable,
// le diagnostic de satinabilite lui-meme, avant meme de generer les rails.
struct RegionSatinabilityVerdict {
    std::size_t path_index{0};
    // Absent si la region n'a jamais ete isolee par `split_region` (chemin
    // reporte dans `unresolved_paths` a la phase 3) -- il n'y a alors rien a
    // analyser.
    std::optional<auto_satin::SatinabilityReport> report;
    bool ready_for_generation{false};
    std::string note;  // renseigne quand `report` est absent, ou en cas d'echec d'analyse
};

// Reanalyse une SatinRegion deja decoupee avec le pipeline existant.
// `ready_for_generation` est vrai ssi le statut resultant est `Suitable` ou
// `SuitableWithWarnings` -- c'est-a-dire que le squelette de CETTE region,
// pris isolement, est bien redevenu proche d'un chemin simple
// endpoint--endpoint (spec SGSD §14), sans quoi la region devrait en
// principe continuer a etre decomposee (hors perimetre de cette phase :
// aucune recursion n'est tentee ici, seulement le diagnostic).
[[nodiscard]] RegionSatinabilityVerdict check_region_satinability(
    const SatinRegion& region, const auto_satin::AutoSatinParameters& params = {});

struct DecompositionSatinabilityReport {
    std::vector<RegionSatinabilityVerdict> verdicts;  // une par SatinRegion isolee a la phase 3
    std::vector<std::size_t> not_ready;               // path_index : verdict negatif OU chemin jamais isole
};

// Applique `check_region_satinability` a chaque region de `split`, et
// reporte aussi automatiquement dans `not_ready` les chemins que la phase 3
// n'a jamais reussi a isoler (`split.unresolved_paths`) -- pas de verdict
// sans region a analyser, mais pas d'omission silencieuse non plus.
[[nodiscard]] DecompositionSatinabilityReport check_all_regions(const RegionSplitReport& split,
                                                                  const auto_satin::AutoSatinParameters& params = {});

[[nodiscard]] std::string format_satinability_report(const DecompositionSatinabilityReport& report);

}  // namespace openstitch::satin_planning
