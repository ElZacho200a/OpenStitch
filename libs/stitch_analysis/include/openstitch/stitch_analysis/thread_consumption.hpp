// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <vector>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/thread_usage.hpp"

namespace openstitch::stitch_analysis {

// =============================================================================
// HP-PROD-005 : consommation de fil par couleur et nombre de canettes (cœur,
// Qt-free, déterministe, aucune E/S).
//
// Point de départ : `thread_usage` (longueur cousue par fil, sauts exclus). Les
// coefficients ci-dessous sont des ORDRES DE GRANDEUR réglables, pas des mesures :
// la consommation réelle dépend du fil, de la tension, du type de point et de
// l'épaisseur du tissu.
// =============================================================================

struct ThreadConsumptionOptions {
    // Le fil du dessus parcourt plus que la distance à plat entre deux piqûres
    // (il descend dans le tissu et remonte) : facteur sur la longueur cousue.
    double top_path_factor{1.15};
    // Fil perdu à chaque passage d'une couleur (enfilage, queues de fil), en mm.
    double waste_per_block_mm{50.0};
    // Fil de canette consommé par mètre de fil du dessus (plage usuelle 0,3 - 0,5 ;
    // le fil du dessus est plus long que celui de canette à cause de la tension).
    double bobbin_per_top_ratio{0.35};
    // Capacité utile d'une canette, en mètres (canette préremplie courante : ~100 m
    // en fil 60 ; à régler selon la canette).
    double bobbin_capacity_m{100.0};
};

struct ThreadConsumption {
    ThreadIdentity identity;
    double sewn_m{0.0};   // longueur cousue à plat (= ThreadUsage::length_mm / 1000)
    double top_m{0.0};    // fil du dessus estimé (facteur + pertes de passage)
    double bobbin_m{0.0}; // fil de canette estimé pour ce fil
};

struct ThreadConsumptionReport {
    std::vector<ThreadConsumption> per_thread; // ordre de première utilisation
    double total_top_m{0.0};
    double total_bobbin_m{0.0};
    // Canettes nécessaires pour le design entier (arrondi à l'entier supérieur ;
    // 0 si aucun fil de canette ou capacité <= 0).
    std::size_t bobbins_needed{0};
    // Part de la dernière canette utilisée, dans ]0, 1] (0 si aucune canette).
    double last_bobbin_fill{0.0};
};

// Version pure sur des usages déjà calculés.
[[nodiscard]] ThreadConsumptionReport
thread_consumption(const std::vector<ThreadUsage>& usage,
                   const ThreadConsumptionOptions& options = {});

// Version complète : `sequence` DOIT être `stitch_generation::effective_sequence(project)`.
[[nodiscard]] ThreadConsumptionReport
thread_consumption(const document::Project& project, const stitch::StitchSequence& sequence,
                   const ThreadConsumptionOptions& options = {});

} // namespace openstitch::stitch_analysis
