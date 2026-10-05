// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/satin_coverage/coverage.hpp"
#include "openstitch/satin_planning/region_split.hpp"
#include "openstitch/satin_planning/topology.hpp"

namespace openstitch::satin_planning {

// Poids de la fonction de cout de decomposition (§ refonte topologique,
// docs/source/satin.md). Points de depart EXPLICITES, pas derives -- a
// recalibrer par la methode "shadow-log" (logger le cout sans changer la
// selection, comparer chaque desaccord avec le selecteur actuel un par un
// avant de changer un defaut) une fois le corpus complet observe, jamais un
// ajustement a l'aveugle sur le taux de reussite agrege.
struct DecompositionCostWeights {
    double coverage{2.0}; // dominant : un trou visible est le pire defaut visible
    double continuity{
        1.0}; // meme echelle que ContinuationCostWeights::angle (poids 1.0, signal le plus fiable)
    double angle{0.6};
    double width{0.6};
    double overlap{0.8};
    double density{0.4};
    double rail_quality{0.5};
    double routing{0.3};
};

// Cout complet d'un candidat de decomposition, chaque terme expose
// separement pour le diagnostic (§ logs de scoring, jamais un score opaque).
//
// SEULS `coverage_cost` et `continuity_cost` sont reellement MESURES par
// `evaluate_decomposition_cost` dans cette premiere integration (etape 3 du
// plan de refonte, jonctions de degre 3 uniquement) -- les deux termes les
// plus lourdement ponderes (2.0/1.0 sur un total de poids de 6.4), donc deja
// un signal reel et utilisable seul. Les cinq autres restent a 0.0,
// EXPLICITEMENT, jusqu'a leur integration :
//  - angle/width/rail_quality necessitent une mesure PAR COLONNE construite
//    (satin_column_view sur le resultat de build_satin_columns) ;
//  - overlap/density necessitent la phase 8 (overlap.hpp) ;
//  - routing necessite la phase 9 (region_routing.hpp).
// Jamais fabrique a partir d'une hypothese non mesuree -- cf. §33 de la
// mission SGSD ("la preuve finale est empirique").
struct DecompositionCost {
    double coverage_cost{0.0};
    double continuity_cost{0.0};
    double angle_cost{0.0};
    double width_cost{0.0};
    double overlap_cost{0.0};
    double density_cost{0.0};
    double rail_quality_cost{0.0};
    double routing_cost{0.0};
    double total{0.0};
};

struct DecompositionCostParams {
    DecompositionCostWeights weights{};
    CutCandidateParams cutParams{}; // rejoue split_region sur ce candidat
    auto_satin::SatinColumnsParameters
        genParams{}; // construit reellement les colonnes (evaluate_region_generation)
    satin_coverage::SatinCoverageConfig coverageConfig{};
    Micrometers density{400};
};

// Evalue le cout complet d'UN candidat de decomposition sur la region source
// `region`/`graph` (§ refonte topologique). Reconstruit REELLEMENT ce
// candidat (split_region + evaluate_decomposition_generation, memes
// fonctions que le reste du pipeline SGSD) plutot qu'un proxy
// pre-construction seul -- coherent avec la discipline "construire puis
// mesurer" deja en place (region_oracle/beam_search). Cout d'autant plus
// eleve que le candidat couvre mal la region ET/OU force une continuite peu
// naturelle entre branches.
//
// `continuity_cost` = moyenne de ContinuationCost::total sur les jonctions
// de `candidate.topology` dont `selected_pair` est non vide (recupere
// directement depuis `JunctionPairingReport::candidates`, deja calcule --
// jamais recalcule), plus une penalite FIXE documentee (1.0, "ne jamais
// rendre l'abandon de continuite gratuit") pour chaque jonction dont
// `selected_pair` est vide (candidat "aucun trunk").
[[nodiscard]] DecompositionCost
evaluate_decomposition_cost(const geometry::PathSet& region, const SkeletonGraph& graph,
                            const DecompositionCandidate& candidate,
                            const DecompositionCostParams& params = {});

// Rendu textuel structure (meme convention que format_merge_pass_report/
// format_overlap_report) : un candidat par bloc, chaque terme de cout non
// nul et le total, la selection finale. Aucune constante magique
// injustifiee : chaque poids est documente dans DecompositionCostWeights.
[[nodiscard]] std::string format_decomposition_cost_report(
    const std::vector<std::pair<DecompositionCandidate, DecompositionCost>>& scored,
    std::optional<std::size_t> selected);

} // namespace openstitch::satin_planning
