// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/satin_coverage/coverage.hpp"
#include "openstitch/satin_planning/satin_plan.hpp"

// §4 de la mission de durcissement du contrat SatinPlanner (2026-08-17) :
// cet adaptateur (`SatinPlan` -> `document::SatinParams` prets a devenir des
// `EmbroideryObject`) vivait jusqu'ici dans `libs/autodigitize`, alors qu'il
// ne depend d'AUCUNE primitive d'auto-classification d'image (segmentation,
// vectorisation) -- accident historique documente dans l'audit Phase 1 :
// ecrit pour l'auto-numerisation, puis reutilise tel quel par les creations
// satin manuelles du desktop, qui devaient de ce fait linker transitivement
// tout `autodigitize` (donc `segmentation`+`vectorisation`) pour une simple
// conversion generique. Deplace ici pour qu'AUCUN workflow satin generique
// (manuel, conversion de type, ligne de coupe) n'ait plus besoin de conna
// itre le module d'auto-classification -- `autodigitize` reste l'unique
// appelant pour son propre usage (auto-numerisation), au meme titre que
// n'importe quel autre appelant desormais.
namespace openstitch::satin_planning {

// Convertit une colonne squelette (`auto_satin::SatinColumnGeometry`, mode
// Legacy, OU `auto_satin::ParametricSatinObject`, mode Parametric -- memes
// noms de champs rail_a/rail_b/rungs/section_*/*_junction, cf.
// libs/auto_satin/include/openstitch/auto_satin/satin_column.hpp) en
// `document::SatinParams`. Point de conversion UNIQUE partage par tous les
// appelants -- `stitch_generation::fill_satin_columns` aplatit
// `rail_a`/`rail_b` (Bezier ou polyligne dense indifferemment) a la demande,
// donc un seul point d'entree suffit pour les deux modes.
template <typename ColumnLike>
[[nodiscard]] document::SatinParams satin_params_from_column(const ColumnLike& col, Micrometers density,
                                                              Micrometers pull_compensation,
                                                              bool center_underlay,
                                                              Micrometers max_width) {
    document::SatinParams sp;
    sp.rail_a = col.rail_a;
    sp.rail_b = col.rail_b;
    sp.density = density;
    sp.pull_compensation = pull_compensation;
    sp.center_underlay = center_underlay;
    sp.max_width = max_width;
    sp.rungs.reserve(col.rungs.size());
    for (const auto& rung : col.rungs) {
        sp.rungs.push_back(document::SatinRung{rung.a, rung.b});
    }
    if (col.section_count > 1 || col.start_junction || col.end_junction) {
        sp.topology = document::SatinSectionTopology{col.section_index, col.section_count,
                                                     col.start_junction, col.end_junction};
    }
    return sp;
}

// Une section satin deja convertie en geometrie editable
// (`document::SatinParams`) et en bande approximative de la surface qu'elle
// couvre (utile a l'appelant pour calculer un eventuel repli, ex. tatami sur
// le reliquat non couvert). Vue UNIFORME sur `auto_satin::SatinColumnGeometry`
// (Legacy) et `auto_satin::ParametricSatinObject` (Parametric).
struct BuiltSatinSection {
    document::SatinParams params;
    geometry::Path strip;
};

// Resultat de `build_satin_sections` : les sections construites, plus le
// contexte necessaire pour un apercu ou un diagnostic cote appelant.
//
// Contrat (§1-33 du plan de refonte satin, 2026-08-14 ; durci §6-7,
// 2026-08-17) : quand SATIN est l'intention (forcee par l'utilisateur ou
// recommandee automatiquement), ce module ne decide JAMAIS de la remplacer
// par du tatami ni de rien refuser silencieusement. `sections` porte tout ce
// qui a pu etre satisfait ; `unresolved_residual` porte, honnetement, tout ce
// qui ne l'a pas ete (geometrie brute, jamais transformee) -- c'est a
// l'APPELANT de decider quoi faire du residu (proposer un choix a
// l'utilisateur, ou, pour un pipeline automatique sans utilisateur present,
// appliquer une politique de repli explicite et non silencieuse). `status`
// expose le verdict EXPLICITE du planner sous-jacent (`SatinPlanStatus`) --
// ne jamais deduire le succes de `sections.empty()`/`unresolved_residual.
// empty()` seuls.
struct SatinBuildReport {
    std::vector<BuiltSatinSection> sections;
    // Vrai si la region a necessite une decomposition (planner recursif,
    // `create_satin_plan` -- au moins une region du plan a une profondeur >
    // 0, ou vient de la reparation de residu). Faux si le solveur local a
    // suffi tel quel sur la region entiere.
    bool used_sgsd{false};
    // Vrai si `unresolved_residual` est non vide -- conserve pour
    // compatibilite avec les appelants existants qui ne consultent que ce
    // booleen ; `unresolved_residual` porte l'information complete.
    bool structural_gap{false};
    // Composantes manquantes significatives qu'AUCUNE tentative de
    // reparation recursive n'a pu resoudre (§12/§17/§18 du plan de refonte) :
    // geometrie brute, jamais convertie en tatami ni en quoi que ce soit par
    // ce module. Un appelant qui les ignore silencieusement reproduirait
    // exactement le defaut que ce contrat existe pour eliminer.
    std::vector<geometry::PathSet> unresolved_residual;
    // Couverture mesuree sur la region SOURCE entiere (union de toutes les
    // sections produites) -- absent si jamais mesuree (aucune section
    // produite).
    std::optional<satin_coverage::SatinCoverageReport> aggregate_coverage;
    std::vector<std::string> warnings;
    // Diagnostic de satinabilite de la region ENTIERE, calcule une seule
    // fois en amont -- toujours renseigne sauf echec d'analyse pur (forme
    // degeneree). Utile pour un apercu utilisateur (statut, largeurs, nombre
    // de branches) meme quand la region a ensuite ete decomposee en
    // plusieurs sous-regions.
    std::optional<auto_satin::SatinabilityReport> whole_region_report;
    // Message de refus si `sections` est vide (rien n'a pu etre construit du
    // tout) -- vide si `sections` est non vide, meme si `unresolved_residual`
    // ne l'est pas (couverture partielle, pas un refus total).
    std::string refusal;
    // §6-7 de la mission de durcissement du contrat (2026-08-17) : verdict
    // explicite du planner sous-jacent, propage tel quel -- la SEULE source
    // de verite pour distinguer un succes complet d'un resultat partiel.
    SatinPlanStatus status{SatinPlanStatus::Incomplete};
    std::vector<PlanningDiagnostic> diagnostics;
};

// Construit reellement les colonnes satin sur `region` en passant par le
// planner recursif unifie (`create_satin_plan`, ce meme module) : tente le
// solveur local, mesure sa couverture reelle, et decompose puis replanifie
// RECURSIVEMENT chaque sous-region tant que la couverture mesuree ne suffit
// pas -- une region fille encore mediocre peut elle-meme etre redecoupee
// (§10 du plan de refonte satin, 2026-08-14). Une passe de reparation de
// residu tente ensuite de replanifier toute composante manquante
// significative comme une nouvelle region (§17).
//
// Ne produit JAMAIS de repli automatique (tatami ou autre) : ce qui reste
// non resolu apres ce processus est expose tel quel dans
// `SatinBuildReport::unresolved_residual`, jamais silencieusement comble
// (§12 du plan de refonte -- « aucun fallback silencieux vers tatami »).
//
// Point d'entree UNIQUE, generique -- AUCUNE dependance conceptuelle a
// l'auto-classification d'image (§4 de la mission de durcissement du
// contrat) : utilisable aussi bien par `autodigitize::auto_digitize` que par
// les creations satin manuelles, conversions de type, ou l'outil ligne de
// coupe du desktop, sans qu'aucun de ces appelants n'ait besoin de lier
// `autodigitize`. `warningLabel`, si non vide, prefixe chaque message de
// `warnings` (ex. "Region 12" cote auto-numerisation).
[[nodiscard]] SatinBuildReport build_satin_sections(const geometry::PathSet& region,
                                                    const auto_satin::SatinColumnsParameters& genParams,
                                                    Micrometers density, Micrometers pullCompensation,
                                                    bool centerUnderlay, Micrometers maxWidth,
                                                    const std::string& warningLabel = {});

}  // namespace openstitch::satin_planning
