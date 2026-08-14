// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/satin_coverage/coverage.hpp"
#include "openstitch/satin_planning/overlap.hpp"
#include "openstitch/satin_planning/region_split.hpp"

namespace openstitch::satin_planning {

// Configuration du planner recursif (§10 du plan de refonte satin, 2026-08-14
// -- "region + intention SATIN -> partition satinable", pas un simple
// reparateur de junctions).
struct SatinPlanConfig {
    auto_satin::SatinColumnsParameters genParams{};
    satin_coverage::SatinCoverageConfig coverageConfig{};
    // Reglages de decoupe reutilises a CHAQUE niveau de recursion (recherche
    // guidee par l'oracle injectee automatiquement si `cutParams.selector`
    // est vide -- un appelant peut fournir sa propre strategie).
    CutCandidateParams cutParams{};
    Micrometers density{400};
    // Garde-fou anti-recursion infinie -- jamais une autorisation, seulement
    // une limite de temps de calcul.
    int max_recursion_depth{4};
    // Seuil MINIMAL de couverture brute pour qu'un "meilleur effort local"
    // (aucune decomposition possible/utile, ou profondeur maximale atteinte)
    // soit accepte comme une feuille du plan plutot que rapporte comme
    // residu. Defaut reel trouve et corrige le 2026-08-14 (diagnostic
    // tentabrode.png, image reelle) : sans ce seuil, une colonne unique ne
    // couvrant que 1,2% d'un gros blob sans jonction interne (donc jamais
    // decomposable) etait acceptee telle quelle comme "le mieux possible" --
    // fabriquant silencieusement une "reussite" a 1,2% que ni l'appelant ni
    // l'utilisateur ne pouvait distinguer d'un vrai succes. En dessous de ce
    // seuil, la region est desormais rapportee comme residu (jamais
    // silencieusement acceptee), meme si aucune decomposition n'est
    // possible -- c'est a l'appelant de decider quoi en faire (§12 du plan
    // de refonte satin), pas a ce module de pretendre avoir reussi.
    double min_fallback_coverage_ratio{0.5};
    // Reparation de residu (§17) : apres assemblage complet, mesure la
    // couverture AGREGEE sur la region source entiere et tente de
    // replanifier chaque composante manquante significative comme une
    // region a part entiere -- jamais un comblement automatique satin/tatami,
    // une vraie tentative recursive comme n'importe quelle autre region.
    //
    // "Significative" est deliberement un seuil MIXTE (fixe ET proportionnel
    // a l'aire de la region source, `max(floor, ratio * aire_source)`), pas
    // une simple aire minimale -- meme raisonnement et memes valeurs par
    // defaut que le correctif de mesure reelle de `autodigitize::
    // build_satin_sections` (2026-08-14) : un rail satin (surtout en mode
    // Parametric) approxime toujours la forme source avec un reliquat
    // NATUREL aux pointes/jonctions, meme quand tout reussit. Un seuil
    // purement absolu trop bas confond ce bruit avec un vrai trou -- verifie
    // empiriquement : un simple rectangle satine correctement se voyait
    // artificiellement redecoupe en 3 regions avec un seuil fixe de 0,5 mm2
    // (les deux reliquats de pointe, chacun negligeable en proportion,
    // depassaient ce seuil en absolu).
    double residual_repair_min_area_mm2{1.0};
    double residual_repair_min_area_ratio{0.03};
    // Filtre supplementaire : une composante manquante fine et allongee (un
    // simple liseré d'arrondi le long d'un rail) peut accumuler une aire
    // non negligeable sans jamais representer un vrai trou -- `max_gap_radius_mm`
    // (rayon du plus grand disque inscrit, deja calcule par
    // `satin_coverage`) caracterise la PROFONDEUR reelle du manque, pas
    // seulement sa surface. En dessous de ce rayon, la composante est
    // ignoree meme si elle depasse le seuil d'aire.
    double residual_repair_min_gap_radius_mm{0.15};
    int max_residual_repair_rounds{2};
    // Recouvrement de couture entre paires adjacentes (§19/§20 du plan de
    // refonte, phase 8 SGSD deja implementee -- `generate_overlaps` --
    // simplement jamais reliee au planner recursif jusqu'ici). Desactivable
    // pour un appelant qui ne veut que la geometrie structurelle brute.
    bool compute_overlaps{true};
    Micrometers overlap_distance{300};
    // §14 du plan de refonte satin (deuxieme famille de coupes candidates,
    // 2026-08-14) : reutilise les `JunctionSeparatorInfo` DEJA calcules par
    // un appel Legacy dedie (cf. `CutCandidateParams::junction_separators`,
    // region_split.hpp) comme distances de coupe candidates supplementaires,
    // prioritaires sur le balayage regulier existant. Desactivable pour
    // eviter le cout d'un appel `build_satin_columns` (mode Legacy force)
    // supplementaire par niveau de recursion, ou pour isoler l'ancienne
    // seule famille (tests de non-regression, comparaison A/B).
    bool use_junction_separator_cuts{true};
    // §18 du plan de refonte satin (phase 7 SGSD, 2026-08-14) : reconsidere
    // CHAQUE coupe reussie a posteriori (`satin_planning::evaluate_merge_pass`,
    // deja teste et fonctionnel, simplement jamais appele par le planner
    // recursif jusqu'ici) -- si les deux regions separees ne font pas
    // mieux (au-dela de `merge_pass_coverage_tolerance`) que leur union,
    // prefere le SEUL segment fusionne : "le plus petit nombre de segments
    // permettant une couverture et un satin corrects" plutot que la
    // premiere partition valide trouvee par le beam search. Desactivable
    // pour eviter le cout d'une evaluation de fusion supplementaire par
    // coupe, ou pour isoler le comportement "garder toute coupe reussie".
    bool use_merge_pass{true};
    double merge_pass_coverage_tolerance{0.02};
};

// Une region finale du plan : geometrie STRUCTURELLE (jamais modifiee pour
// mesurer la couverture, cf. §20 -- une geometrie de couture dediee avec
// recouvrement reste un raffinement ulterieur, non encore produit ici),
// colonnes REELLEMENT construites, et le verdict de couverture qui a motive
// son acceptation.
struct SatinPlanRegion {
    geometry::PathSet region;
    auto_satin::SatinColumnsResult columns;
    // Absent seulement si le solveur local n'a produit aucune colonne du
    // tout (rien a mesurer) -- ne devrait normalement pas apparaitre dans
    // `SatinPlan::regions` (une region sans colonne n'est jamais acceptee
    // comme feuille), conserve optionnel par prudence plutot que par un
    // rapport par defaut trompeur (couverture 0% qui se lirait comme
    // "mesuree et mauvaise" plutot que "jamais mesuree").
    std::optional<satin_coverage::SatinCoverageReport> coverage;
    int depth{0};
    // Vrai si cette region vient de la boucle de reparation de residu
    // (§17), pas de la decomposition initiale -- utile pour le diagnostic
    // utilisateur ("3 regions initiales + 1 region de reparation").
    bool from_residual_repair{false};
};

// Geometrie de couture DEDIEE (§19/§20) pour une paire de `SatinPlan::adjacency` --
// chaque cote elargi vers l'autre pour fermer l'interstice physique laisse
// par la coupe qui les a separes, puis recadre dans la geometrie exacte
// d'avant-coupe (cf. `overlap.hpp`). Distincte de `SatinPlanRegion::region`
// (jamais modifiee, reste la reference pour toute mesure de couverture) --
// c'est une geometrie A USAGE DE GENERATION DE POINTS uniquement. Repli sur
// la region non modifiee (pas d'agrandissement) si le recouvrement n'a pas pu
// etre calcule -- toujours alignee 1:1 sur `SatinPlan::adjacency`, jamais un
// vecteur plus court qui desynchroniserait les deux.
struct SatinPlanOverlap {
    geometry::PathSet first_extended;
    geometry::PathSet second_extended;
};

struct SatinPlan {
    std::vector<SatinPlanRegion> regions;
    // Paires d'index DANS `regions` connues comme directement adjacentes :
    // deux regions finales (feuilles du plan, eventuellement issues de
    // profondeurs de recursion differentes) separees par une seule et meme
    // coupe, ET restees toutes deux des feuilles jusqu'a la fin de LEUR
    // propre sous-arbre de decomposition. Peuple depuis le 2026-08-14 (§19) :
    // chaque niveau de recursion (initial ET reparation de residu, §17)
    // rapporte ses propres paires, reindexees a la volee vers l'index final
    // dans ce vecteur. Limite connue : une paire dont un cote a ete
    // redecoupe ulterieurement (donc devenu plusieurs feuilles) n'est PAS
    // rapportee -- meme restriction que `RegionSplitReport::merge_candidates`
    // en amont ; determiner l'adjacence a travers un redecoupage reste un
    // travail futur.
    std::vector<std::pair<std::size_t, std::size_t>> adjacency;
    // Alignee 1:1 sur `adjacency` (memes indices) : la geometrie de
    // recouvrement de chaque paire, si `SatinPlanConfig::compute_overlaps`
    // est actif. Le module ne fait QUE calculer cette geometrie -- il ne
    // fusionne, ne route, ni ne genere de points dessus : c'est a
    // l'appelant (generation de points, §20) d'en faire usage.
    std::vector<SatinPlanOverlap> overlaps;
    // Couverture mesuree sur la region SOURCE entiere par l'union de toutes
    // les colonnes de `regions` -- absent si jamais mesuree (aucune region
    // acceptee).
    std::optional<satin_coverage::SatinCoverageReport> aggregate_coverage;
    // Composantes manquantes significatives qu'aucune tentative de
    // replanification n'a pu resoudre. JAMAIS comblees automatiquement
    // (satin ou tatami) par ce module -- §12/§18 du plan de refonte : la
    // couverture sert de TEST, jamais de mecanisme pour fabriquer
    // artificiellement 100%, et l'appelant garde la decision explicite sur
    // ce qu'il advient de ce residu.
    std::vector<geometry::PathSet> unresolved_residual;
    // Avertissements accumules depuis CHAQUE appel au solveur local, dans
    // tout l'arbre de recursion -- jamais perdus silencieusement (meme
    // garantie "jamais silencieux" que le reste du pipeline satin).
    std::vector<std::string> warnings;
    std::string diagnostic;
};

// Point d'entree UNIQUE du planner satin (§32 du plan de refonte, 2026-08-14) :
// prend une region ARBITRAIRE (n'importe quelle forme demandee en satin par
// l'utilisateur, jamais pre-filtree par un seuil de largeur agrege) et
// produit une partition de regions que le solveur local
// (`auto_satin::build_satin_columns`) sait effectivement bien transformer en
// satin, mesure empiriquement via `satin_coverage::analyze_satin_coverage` --
// jamais une regle theorique rigide sur la topologie du squelette.
//
// Algorithme (§10, recursif) : pour chaque region (en commencant par
// `source`), tente d'abord le solveur local ET mesure sa couverture reelle.
// Si elle est bonne (`SatinCoverageReport::passed`) ou que la profondeur
// maximale est atteinte, la region est acceptee telle quelle. Sinon, si son
// squelette contient au moins une jonction, elle est decoupee
// (`decompose_into_paths` + `split_region`, recherche guidee par l'oracle) et
// CHAQUE sous-region resultante est replanifiee recursivement -- une region
// fille peut elle-meme necessiter un nouveau decoupage. Si la decomposition
// ne produit rien d'exploitable (aucune jonction, ou aucune coupe valide),
// le meilleur effort local est conserve tel quel plutot que d'etre perdu.
//
// Une fois l'arbre de decomposition epuise, une passe de reparation de
// residu (§17) mesure la couverture AGREGEE sur `source` entiere et tente de
// replanifier chaque composante manquante significative comme une nouvelle
// region -- jusqu'a `max_residual_repair_rounds` passes, ou jusqu'a ce
// qu'aucun progres ne soit plus possible.
[[nodiscard]] SatinPlan create_satin_plan(const geometry::PathSet& source, const SatinPlanConfig& config = {});

[[nodiscard]] std::string format_satin_plan(const SatinPlan& plan);

}  // namespace openstitch::satin_planning
