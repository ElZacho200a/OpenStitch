// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/satin_coverage/coverage.hpp"
#include "openstitch/satin_planning/concavity_cuts.hpp"
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
    // §20 du plan de refonte satin (2026-08-16) : consomme reellement
    // `SatinPlan::overlaps` (ci-dessus, calcule mais jusqu'ici jamais
    // utilise) -- reconstruit les colonnes de chaque region sur sa
    // geometrie de recouvrement DEDIEE quand un voisin direct est connu,
    // pour fermer visuellement l'interstice de coupe entre les deux.
    // N'affecte JAMAIS la mesure de couverture (toujours sur la geometrie
    // structurelle) : repli automatique sur les colonnes d'origine si la
    // reconstruction echoue ou couvre moins bien la region structurelle que
    // la version d'origine. Sans effet si `compute_overlaps` est desactive
    // (rien a consommer).
    bool extend_columns_into_overlap{true};
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
    // §14 du plan de refonte satin, suite (2026-08-14) : quand une region
    // n'a AUCUNE jonction de squelette (donc rien pour les deux familles de
    // coupe precedentes) et que le solveur local echoue quand meme, tente
    // une coupe ancree sur une concavite du CONTOUR lui-meme
    // (`generate_concavity_cut_candidates`) avant de renoncer -- cible le
    // cas d'une entaille profonde ou d'un sablier sans topologie de
    // squelette exploitable (ex. `notch`/`pinch` du corpus de test).
    bool use_concavity_cuts{true};
    ConcavityCutParams concavityCutParams{};
    std::size_t concavity_cut_beam_width{6};

    // Budgets d'exploration EXPLICITES (mission de durcissement du contrat,
    // 2026-08-17, §18) : `max_recursion_depth` ci-dessus borne deja la
    // PROFONDEUR d'une branche individuelle, mais rien ne bornait jusqu'ici
    // le VOLUME total de travail sur tout l'arbre de recursion -- une forme
    // suffisamment pathologique (beaucoup de jonctions, beaucoup de
    // reparations de residu) pouvait explorer un nombre de regions non
    // borne. Quand un budget est atteint, le planner s'arrete proprement et
    // rapporte honnetement `SatinPlanStatus::Incomplete` (raison
    // `SearchBudgetExceeded` dans `SatinPlan::diagnostics`) -- jamais un
    // `Complete` fabrique en coupant court silencieusement.
    // Defauts abaisses (2026-08-17, corpus de torture) : une jonction a
    // HAUT DEGRE (5+ branches se rejoignant en UN SEUL point, ex. fixture
    // "star5") s'est reveleee couteuse par ITERATION (generation de
    // candidats de coupe du planner, plusieurs secondes par tentative,
    // meme a `beam_width=1`) -- un budget en NOMBRE d'iterations calibre
    // pour des regions "simples" (§ ancien defaut 500) pouvait donc encore
    // representer plusieurs MINUTES dans le pire cas. Cause isolee (§33 :
    // le solveur local seul, `auto_satin::build_satin_columns`, reste
    // rapide sur la meme forme -- moins d'une seconde) mais NON corrigee a
    // la racine (generation de candidats de `satin_planning::split_region`
    // sur une jonction a haut degre) : correctif architectural identifie
    // mais delibereement reporte, cf. §33 de la mission ("ne pas patcher
    // opportunement le generateur/le planner sous la pression d'une seule
    // fixture, sans l'avoir clairement classe"). En attendant, ces defauts
    // bornent le temps TOTAL raisonnablement, quitte a rapporter Incomplete
    // plus tot sur les formes a jonction a haut degre -- jamais a laisser
    // tourner indefiniment.
    int max_total_regions{40};
    int max_planning_iterations{60};
    // Filet de securite EN PLUS des compteurs deterministes ci-dessus
    // (§18 : "ne pas utiliser le temps wall-clock comme SEULE limite dans
    // les tests deterministes" -- ceci n'en est PAS la seule limite, juste
    // une garde-fou supplementaire de defense en profondeur) : borne le
    // temps mur total d'un appel a `create_satin_plan`, quel que soit le
    // cout reel par iteration -- jamais de dependance UNIQUE au nombre
    // d'iterations pour borner le temps, qui s'est reveleee insuffisante en
    // pratique (cf. commentaire ci-dessus).
    //
    // COMPROMIS DE DETERMINISME ASSUME (§19 de la mission) : pour une forme
    // dont ce filet wall-clock est effectivement le facteur limitant (le
    // temps ecoule reel varie d'une execution a l'autre, contrairement aux
    // compteurs deterministes ci-dessus), le plan resultant N'EST PAS
    // garanti identique a l'octet pres entre deux executions -- compromis
    // deliberement accepte (securite avant determinisme parfait sur les cas
    // deja pathologiques), documente honnetement plutot que cache. Les
    // formes qui NE declenchent JAMAIS ce filet (l'immense majorite du
    // corpus) restent entierement deterministes.
    // Essaye brievement a 20'000 le 2026-08-17 (raisonnement initial :
    // couvrir la marge Debug observee sur "cross"/"h", cf. docs/source/
    // satin.md) puis REVERTE a 10'000 -- verifie empiriquement (§37, ne pas
    // supposer) que ce raisonnement ne s'appliquait PAS a toutes les
    // formes : sur le reseau en T de `test_autodigitize`, le temps ecoule
    // reel a la limite SUIT le plafond configure (~15,4s a 10s de plafond,
    // ~29,6s a 20s de plafond) au lieu de se stabiliser -- signe d'un cout
    // qui CROIT avec le budget disponible (meme famille que comb/star5/E),
    // pas d'un simple appel ponctuel un peu lent (contrairement a
    // "cross"/"h", qui eux se stabilisent). Augmenter le plafond global
    // n'aide donc PAS ce genre de cas (juste plus lent avant le meme
    // echec), et ralentit inutilement toute la suite de tests sur les cas
    // deja pathologiques. Le vrai defaut revele par ce cas (`unresolved_
    // residual` qui peut se vider a tort quand le budget s'epuise PENDANT
    // une reparation de residu) est corrige a la source (cf. `create_satin_
    // plan`, derivation de `unresolved_residual` depuis la mesure finale)
    // plutot que masque en repoussant le plafond.
    int max_planning_wall_clock_ms{10'000};
    // §18 de la mission de durcissement du contrat (2026-08-17) : borne le
    // cout de `OracleGuidedSelector`, le selecteur PAR DEFAUT de chaque
    // decomposition -- documente lui-meme (beam_search.hpp) comme
    // "reserve a un usage hors ligne (CLI de debug, tests), jamais au
    // chemin interactif", car son cout est `beam_width` generations+mesures
    // COMPLETES PAR evenement de detachement. Defaut reel trouve via le
    // corpus de torture (2026-08-17, fixture "comb", 6 jonctions) : une
    // seule tentative de decomposition prenait plusieurs dizaines de
    // secondes -- ce cout croit avec le nombre d'evenements de detachement,
    // donc avec le nombre de jonctions de la region, sans jamais avoir ete
    // borne jusqu'ici. `beam_width` est le reglage utilise quand la region a
    // PEU de jonctions (qualite de decoupe maximale, cout deja verifie
    // acceptable sur tout le corpus existant) ; au-dela de
    // `max_junctions_for_full_beam_search`, le beam_width effectif retombe a
    // 1 (le premier candidat valide construit, jamais un choix aveugle
    // avant construction -- seulement moins de candidats compares). Compare
    // au PLUS GRAND de `graph.junction_count()` (noeuds de jonction) et de
    // `DecompositionReport::paths.size()` (evenements de detachement
    // reellement traites) -- une etoile a 5 branches n'a qu'un seul noeud
    // de jonction mais 5 chemins, donc `junction_count()` seul sous-estime
    // le cout reel sur cette topologie precise.
    std::size_t beam_width{3};
    std::size_t max_junctions_for_full_beam_search{4};
    // Garde-fou GLOBAL supplementaire (§18) : meme sur une region locale
    // simple, un plan qui a deja beaucoup decompose (ex. une etoile a 5
    // branches, decomposee par coupes PAIRE-A-PAIRE successives -- chaque
    // sous-region issue d'une coupe peut redevenir "simple" localement tout
    // en faisant partie d'un arbre de decomposition deja couteux) retombe a
    // `beam_width=1` au-dela de ce nombre total d'evaluations a pleine
    // qualite deja effectuees DANS CE PLAN -- le seuil local seul
    // (`max_junctions_for_full_beam_search`) ne suffisait pas a borner le
    // cout CUMULE sur ce cas reel (fixture "star5", corpus de torture
    // 2026-08-17 : encore ~16s apres le premier correctif, seule la racine
    // depassait le seuil local, chaque sous-decomposition ulterieure
    // redemarrant a beam_width plein).
    int max_oracle_evaluations_at_full_beam_width{5};

    // Seuil de couverture GLOBALE pour le verdict `SatinPlanStatus::Complete`
    // (mission de durcissement du contrat, 2026-08-17) -- DELIBEREMENT
    // DISTINCT de `coverageConfig.min_core_coverage`/`min_raw_coverage`
    // (utilises pendant la RECURSION pour decider si une region individuelle
    // merite d'etre acceptee telle quelle ou redecoupee, volontairement
    // stricts pour encourager une meilleure decomposition tant qu'elle reste
    // possible). Defaut trouve necessaire empiriquement (2026-08-17) : meme
    // un rectangle simple, sans aucun defaut de generation, ne franchit pas
    // le seuil de couverture COEUR de `coverageConfig` (0,98995 mesure contre
    // 0,995 requis -- residu naturel aux coins, cf. `docs/source/satin.md`,
    // §13 : "ne jamais considerer 99,5% comme une constante universelle").
    // Utilise la couverture BRUTE (moins sensible a ce residu ponctuel que la
    // couverture coeur) sur la region SOURCE entiere.
    double complete_min_raw_coverage{0.95};
};

// §6-7 de la mission de durcissement du contrat (2026-08-17) : verdict
// EXPLICITE du planner, jamais a deduire indirectement de
// `regions.empty()`/`unresolved_residual.empty()`. Un appelant qui ignore ce
// champ et ne regarde que `regions` ne peut plus confondre un plan
// INCOMPLET avec un succes.
enum class SatinPlanStatus {
    // Toutes les conditions A-E (cf. `SatinPlan::status`) sont satisfaites :
    // aucun residu au-dela du seuil de significativite de la reparation
    // (meme seuil que `residual_repair_min_area_mm2`/`_ratio`, §A), chaque
    // feuille produit reellement des colonnes (§B), couverture BRUTE globale
    // suffisante -- `SatinPlanConfig::complete_min_raw_coverage`, mesuree sur
    // la region SOURCE entiere, jamais une moyenne par region (§C), aucun
    // trou local disproportionne -- `max_gap_radius_mm` de la couverture
    // agregee (§D), aucune geometrie invalide detectee -- rails croises
    // (§E).
    Complete,
    // Au moins une condition manque, mais le planner a produit un resultat
    // partiel exploitable (des `regions` existent, ou existeraient si
    // l'appelant le souhaite) -- le cas normal d'un territoire trop complexe
    // pour etre entierement resolu avec les budgets/seuils configures. C'est
    // a l'appelant de decider (continuer partiel / tatami pour le reliquat /
    // annuler) -- jamais a ce module.
    Incomplete,
    // Extremement rare, cf. mission §6 : budget de recherche EPUISE ET
    // territoire significatif encore non representable en toute securite.
    // PAS "le solveur local direct a rejete le polygone d'origine" (ca,
    // c'est le cas normal qui declenche la decomposition) -- uniquement une
    // exploration reelle et bornee qui n'a pas suffi.
    Impossible,
};

[[nodiscard]] std::string to_string(SatinPlanStatus status);

// Diagnostic structure -- code court + message exploitable, jamais une
// simple chaine opaque perdue dans `SatinPlan::warnings` (qui reste reservee
// aux avertissements du solveur local). Codes stables utilises a ce jour :
// "SearchBudgetExceeded", "InvalidGeometryDetected".
struct PlanningDiagnostic {
    std::string code;
    std::string message;
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

    // §6-7 de la mission de durcissement du contrat (2026-08-17) : verdict
    // explicite, calcule une seule fois en fin de `create_satin_plan` a
    // partir des criteres A-E documentes sur `SatinPlanStatus`. Ne JAMAIS
    // inferer le succes autrement (ex. `regions.empty()`) -- ce champ est la
    // seule source de verite.
    SatinPlanStatus status{SatinPlanStatus::Incomplete};
    std::vector<PlanningDiagnostic> diagnostics;

    // Metriques d'exploration (§25 : rapport de qualite), approximatives par
    // construction -- `regions_explored` compte chaque appel a
    // `plan_recursive` (chaque tentative de solveur local sur une region,
    // acceptee ou non) ; `oracle_evaluations` compte chaque TENTATIVE de
    // decomposition guidee par l'oracle (un appel a `split_region` avec
    // beam search, ou a `select_best_concavity_cut`), pas chaque candidat
    // individuel evalue en son sein (ce compte plus fin resterait interne
    // au beam search lui-meme).
    int regions_explored{0};
    int oracle_evaluations{0};
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
