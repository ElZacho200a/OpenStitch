// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/vector_object.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/satin_coverage/coverage.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::autodigitize {

struct AutoOptions {
    Millimeters mm_per_px{25.4 / 96.0};
    Micrometers simplify_tolerance{200};
    // Largeur moyenne max pour proposer un satin (au-delà : tatami).
    Micrometers satin_max_width{6'000};
    // Aire min (mm²) pour un remplissage ; en dessous, simple contour.
    double min_fill_area_mm2{4.0};
    // Ignore le FOND présumé : la couleur du plus gros morceau segmenté, et
    // TOUTE autre région de cette même couleur exacte (pas seulement ce
    // morceau) -- un fond peut se fragmenter en plusieurs régions disjointes
    // de la même couleur (ex. les zones hors d'un motif rond inscrit dans une
    // image carrée). Particulièrement utile sur une image SANS canal alpha :
    // sans transparence, le fond devient une région opaque comme les autres.
    bool skip_largest_region{false};
    // Active la classification automatique satin/tatami par forme (§24 du
    // plan de refonte satin, 2026-08-14 : "AutoChoice", une RECOMMANDATION
    // automatique de l'auto-numérisation, distincte d'un choix satin
    // explicitement forcé par l'utilisateur ailleurs dans l'application).
    // Quand une région est classée satin, elle passe TOUJOURS par le planner
    // récursif unifié (`satin_planning::create_satin_plan`, via
    // `build_satin_sections` ci-dessous) — jamais un appel direct sur la
    // région entière ni un refus global : cf. docs/source/satin.md.
    bool use_auto_satin{true};
    // Satin automatique NAÏF (rails_from_contour) : désactivé par défaut. Ses
    // deux rails « bouts les plus éloignés » débordent sur les formes concaves
    // ou branchues (les rungs enjambent les creux). Tant que le vrai moteur
    // auto-satin (squelette) ne génère pas la géométrie, on remplit ces zones
    // en tatami — découpé proprement sur la région, donc sans débordement.
    bool use_naive_satin{false};
};

// Objets produits par l'autonumérisation : toujours ÉDITABLES (§13). Le type
// de chaque objet est choisi par la forme de sa région (satin pour les bandes
// fines, tatami pour les zones pleines, contour pour les petites régions).
struct AutoResult {
    std::vector<document::VectorObject> vectors;
    std::vector<document::EmbroideryObject> embroideries;
    // Avertissements non bloquants du moteur auto-satin (squelette), reportés
    // ici pour que l'appelant puisse les montrer à l'utilisateur. Quand une
    // branche de squelette est rejetée (ex. trop large pour du satin, aucune
    // section transversale valide sur son axe), la zone qu'elle couvrait
    // reçoit désormais un remplissage tatami de repli (§ ci-dessous, jamais
    // laissée sans le moindre point) -- ces avertissements restent utiles
    // pour EXPLIQUER pourquoi cette zone est en tatami plutôt qu'en satin
    // comme le reste de la région (ex. un empattement trop large, cf.
    // docs/source/satin.md § lettre en T d'un logo réel). Préfixé par la
    // région source pour localiser le problème.
    std::vector<std::string> warnings;
};

// Construit les objets à partir d'une segmentation. Les identifiants sont
// alloués via `ids` (le générateur du document), donc jamais réutilisés.
[[nodiscard]] Result<AutoResult> auto_digitize(const segmentation::Segmentation& seg,
                                               IdGenerator<ObjectId>& ids,
                                               const AutoOptions& options);

// Convertit une colonne squelette (`auto_satin::SatinColumnGeometry`, mode
// Legacy, OU `auto_satin::ParametricSatinObject`, mode Parametric — mêmes
// noms de champs rail_a/rail_b/rungs/section_*/*_junction, cf.
// libs/auto_satin/include/openstitch/auto_satin/satin_column.hpp) en
// `document::SatinParams`. Point de conversion UNIQUE partagé par
// l'autonumérisation (ci-dessus) et les créations satin manuelles
// (apps/desktop) — `stitch_generation::fill_satin_columns` aplatit
// `rail_a`/`rail_b` (Bézier ou polyligne dense indifféremment) à la demande,
// donc un seul point d'entrée suffit pour les deux modes.
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

// Une section satin déjà convertie en géométrie éditable
// (`document::SatinParams`) et en bande approximative de la surface qu'elle
// couvre (utile à l'appelant pour calculer un éventuel repli, ex. tatami sur
// le reliquat non couvert). Vue UNIFORME sur `auto_satin::SatinColumnGeometry`
// (Legacy) et `auto_satin::ParametricSatinObject` (Parametric).
struct BuiltSatinSection {
    document::SatinParams params;
    geometry::Path strip;
};

// Résultat de `build_satin_sections` : les sections construites, plus le
// contexte nécessaire pour un aperçu ou un diagnostic côté appelant.
//
// Contrat (§1-33 du plan de refonte satin, 2026-08-14) : quand SATIN est
// l'intention (forcée par l'utilisateur ou recommandée automatiquement), ce
// module ne décide JAMAIS de la remplacer par du tatami ni de rien refuser
// silencieusement. `sections` porte tout ce qui a pu être satisfait ;
// `unresolved_residual` porte, honnêtement, tout ce qui ne l'a pas été
// (géométrie brute, jamais transformée) -- c'est à l'APPELANT de décider
// quoi faire du résidu (proposer un choix à l'utilisateur, ou, pour un
// pipeline automatique sans utilisateur présent, appliquer une politique de
// repli explicite et non silencieuse, cf. `auto_digitize` ci-dessus).
struct SatinBuildReport {
    std::vector<BuiltSatinSection> sections;
    // Vrai si la région a nécessité une décomposition (planner récursif,
    // `satin_planning::create_satin_plan` -- au moins une région du plan a
    // une profondeur > 0, ou vient de la réparation de résidu). Faux si le
    // solveur local a suffi tel quel sur la région entière.
    bool used_sgsd{false};
    // Vrai si `unresolved_residual` est non vide -- conservé pour
    // compatibilité avec les appelants existants qui ne consultent que ce
    // booléen ; `unresolved_residual` porte l'information complète.
    bool structural_gap{false};
    // Composantes manquantes significatives qu'AUCUNE tentative de
    // réparation récursive n'a pu résoudre (§12/§17/§18 du plan de refonte) :
    // géométrie brute, jamais convertie en tatami ni en quoi que ce soit par
    // ce module. Un appelant qui les ignore silencieusement reproduirait
    // exactement le défaut que ce contrat existe pour éliminer.
    std::vector<geometry::PathSet> unresolved_residual;
    // Couverture mesurée sur la région SOURCE entière (union de toutes les
    // sections produites) -- absent si jamais mesurée (aucune section
    // produite).
    std::optional<satin_coverage::SatinCoverageReport> aggregate_coverage;
    std::vector<std::string> warnings;
    // Diagnostic de satinabilité de la région ENTIÈRE, calculé une seule
    // fois en amont -- toujours renseigné sauf échec d'analyse pur (forme
    // dégénérée). Utile pour un aperçu utilisateur (statut, largeurs, nombre
    // de branches) même quand la région a ensuite été décomposée en
    // plusieurs sous-régions.
    std::optional<auto_satin::SatinabilityReport> whole_region_report;
    // Message de refus si `sections` est vide (rien n'a pu être construit du
    // tout) -- vide si `sections` est non vide, même si `unresolved_residual`
    // ne l'est pas (couverture partielle, pas un refus total).
    std::string refusal;
};

// Construit réellement les colonnes satin sur `region` en passant par le
// planner récursif unifié (`satin_planning::create_satin_plan`,
// `libs/satin_planning`) : tente le solveur local, mesure sa couverture
// réelle, et décompose puis replanifie RÉCURSIVEMENT chaque sous-région tant
// que la couverture mesurée ne suffit pas — une région fille encore
// médiocre peut elle-même être redécoupée (§10 du plan de refonte satin,
// 2026-08-14), contrairement à l'ancienne décomposition à une seule passe.
// Une passe de réparation de résidu tente ensuite de replanifier toute
// composante manquante significative comme une nouvelle région (§17).
//
// Ne produit JAMAIS de repli automatique (tatami ou autre) : ce qui reste
// non résolu après ce processus est exposé tel quel dans
// `SatinBuildReport::unresolved_residual`, jamais silencieusement comblé
// (§12 du plan de refonte -- « aucun fallback silencieux vers tatami »).
//
// Point d'entrée UNIQUE partagé par l'auto-numérisation (`auto_digitize`
// ci-dessus) et la création satin manuelle (`apps/desktop/main_window.cpp`)
// — mêmes garanties partout, un seul endroit à faire évoluer.
// `warningLabel`, si non vide, préfixe chaque message de `warnings` (ex.
// "Région 12" côté auto-numérisation).
[[nodiscard]] SatinBuildReport build_satin_sections(const geometry::PathSet& region,
                                                    const auto_satin::SatinColumnsParameters& genParams,
                                                    Micrometers density, Micrometers pullCompensation,
                                                    bool centerUnderlay, Micrometers maxWidth,
                                                    const std::string& warningLabel = {});

}  // namespace openstitch::autodigitize
