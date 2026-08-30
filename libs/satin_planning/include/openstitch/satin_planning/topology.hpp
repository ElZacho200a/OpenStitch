// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "openstitch/satin_planning/branch_pairing.hpp"

namespace openstitch::satin_planning {

// Classification d'une portion du squelette (§ refonte decomposition
// topologique, docs/source/satin.md). "Regular" = axe identifiable, deux
// portions de contour opposees, largeur continue, aucune ambiguite
// topologique locale -- exactement le regime ou
// auto_satin::compute_column_stations/cross_section donnent deja de bons
// resultats (97-99% de couverture mesuree sur le corpus SGSD). "Singular" =
// tout le reste : c'est la que la correspondance de contour (etape
// ulterieure de la refonte) est necessaire, pas seulement la ou le
// squelette a un noeud Junction -- une transition de largeur abrupte SANS
// jonction (ex. "pinch"/"notch") est deja singuliere au sens de cette
// classification, meme si SkeletonNodeType n'y voit qu'une arete continue.
enum class RegionClass : std::uint8_t { Regular, Singular };

// Motif de classification (§ jamais un verdict opaque -- cf. logs de
// scoring). Une arete peut cumuler plusieurs raisons.
enum class SingularityReason : std::uint8_t {
    JunctionDegreeGE3,
    AbruptWidthChange,
};

struct RegionClassification {
    RegionClass region_class{RegionClass::Regular};
    std::vector<SingularityReason> reasons;  // vide ssi region_class == Regular
};

// Classifie UNE arete du squelette (§ refonte topologique). `JunctionDegreeGE3`
// est leve si l'une de ses deux extremites est une vraie jonction (degre >=3) --
// la portion de branche la plus proche de ce noeud subit deja le traitement
// special de jonction en aval (amputation/extension, cf. satin_column.cpp),
// donc n'est jamais un cas "regulier" pur. `AbruptWidthChange` compare le
// rayon local minimal au rayon local maximal le long de la centerline
// (`SkeletonEdge::local_radii_um`, deja calcule par le squelette -- aucune
// nouvelle mesure) : un ratio max/min au-dela du seuil signale un
// changement de largeur trop brutal pour la methode axe->normale seule
// (ex. "pinch"/"notch"/"deep_channel", qui ont deja leur propre famille de
// coupe par concavite plutot que la methode de section transversale).
//
// Seuil `width_jump_ratio_threshold` par defaut (2.0, "la largeur double ou
// plus sur la longueur de la branche") : premier point de depart explicite,
// pas derive -- a recalibrer par la methode "shadow-log" (cf. plan de
// refonte) une fois des cas reels observes, jamais ajuste a l'aveugle.
[[nodiscard]] RegionClassification classify_edge(const SkeletonGraph& graph, const SkeletonEdge& edge,
                                                  double width_jump_ratio_threshold = 2.0);

enum class JunctionType : std::uint8_t { T, Y, X, AcuteFork, Merge, LoopConnection, Complex };

// Objet jonction de premiere classe (§ refonte topologique) : `position`/
// `incident_branches` viennent directement du SkeletonNode/SkeletonGraph
// existant -- aucune reimplementation de la detection de jonction (deja
// faite par build_skeleton_graph + prune_graph). `type` est une
// classification DIAGNOSTIQUE (n'affecte aucune decision de coupe/appariement
// aujourd'hui) fondee sur les couts de continuation deja calcules par
// pair_branches_at_junction -- "classification imparfaite en v1" est
// explicitement accepte (cf. plan de refonte, §2).
struct SatinJunction {
    std::uint32_t node_id{0};
    Vec2um position{};
    std::vector<std::uint32_t> incident_branches;  // == SkeletonEdge::id, tries croissants
    JunctionType type{JunctionType::Complex};
};

// Classifie une jonction (noeud de degre >=2 -- degre <2 n'est pas une
// jonction et renvoie Complex par defense, ne devrait jamais arriver en
// pratique puisque seuls les noeuds SkeletonNodeType::Junction, degre >=3
// par construction du graphe, sont censes etre passes ici).
//
// Regle (fondee sur angle_cost, le signal le plus fiable de
// ContinuationCostWeights -- poids 1.0 dans le systeme existant) :
//  - degre 3 : les 3 appariements possibles sont compares. Si le meilleur
//    est nettement plus bas que les deux autres (ecart >= t_gap), une paire
//    domine clairement -> T (deux branches quasi colineaires, une troisieme
//    qui vient s'y terminer). Si les 3 couts sont proches les uns des
//    autres (ecart max-min < t_close) -> Y (aucune paire n'est une
//    continuation naturelle privilegiee). Sinon -> AcuteFork (cas
//    intermediaire, ni un T net ni un Y symetrique).
//  - degre 4 : les 3 partitions possibles en deux paires DISJOINTES sont
//    comparees par la somme de leurs deux angle_cost. Si une partition
//    domine nettement -> X (deux paires traversantes). Sinon -> Complex.
//  - degre >=5 ou degre <3 : Complex (hors de portee de cette premiere
//    passe, cf. plan de refonte, etapes 5-6).
//
// Seuils par defaut (t_gap=0.20, t_close=0.15) : points de depart explicites
// calibres sur les fixtures synthetiques `t`/`y`/`cross` du corpus
// (angle_cost quasi 0 pour une paire colineaire, quasi 0.5 pour une paire
// perpendiculaire, quasi 0.25 pour trois branches a 120°) -- PAS derives
// analytiquement, a affiner par la methode shadow-log sur le corpus complet.
struct JunctionClassificationParams {
    double dominant_pair_gap{0.20};
    double symmetric_pair_closeness{0.15};
};

[[nodiscard]] SatinJunction classify_junction(const SkeletonGraph& graph, std::uint32_t junctionNode,
                                               const ContinuationCostParams& costParams = {},
                                               const JunctionClassificationParams& classParams = {});

// Une decomposition topologique CANDIDATE pour une jonction donnee :
// generalisation de decompose_into_paths, qui n'en produit qu'une (l'argmin
// de pair_branches_at_junction). `topology` reutilise le type EXISTANT tel
// quel (DecompositionReport) -- obtenu en rejouant decompose_into_paths
// avec un JunctionOverride cible sur cette seule jonction, le reste du
// graphe gardant son comportement naturel.
struct DecompositionCandidate {
    std::uint32_t candidate_id{0};
    DecompositionReport topology;
    std::string description;  // "trunk = aretes 0/1 (cout 0.042)", lisible humain
};

struct DecompositionCandidateSet {
    std::uint32_t junction_node{0};
    std::vector<DecompositionCandidate> candidates;
};

// Genere, pour un noeud de jonction donne, les variantes de
// decompose_into_paths dignes d'etre comparees par un cout complet (une
// fois decomposition_cost.hpp integre) -- pas seulement l'argmin de
// pair_branches_at_junction. Reutilise JunctionPairingReport::candidates
// (deja calcule par pair_branches_at_junction, jamais exploite au-dela de
// son premier element avant cette refonte) : chaque PairCandidate valide,
// par cout croissant, devient une variante "cette paire est le trunk",
// PLUS une variante finale "toutes les branches independantes" (jamais
// generee avant cette refonte).
//
// Bornee par `max_candidates_per_junction` (defaut 3 : les 2 meilleurs
// appariements + la variante independante) -- un degre eleve (croix,
// etoile) a C(degre,2) appariements possibles, largement plus que ce qui
// merite une construction+mesure complete une fois le scoring reel branche
// (§ risque budget, plan de refonte). candidate_id est numerote dans
// l'ordre de generation (argmin d'abord, runners-up par cout croissant,
// variante independante en dernier) -- jamais un ordre dependant d'un hash,
// deterministe par construction (meme discipline que
// pair_branches_at_junction::candidates).
[[nodiscard]] DecompositionCandidateSet enumerate_decomposition_candidates(
    const SkeletonGraph& graph, std::uint32_t junctionNode, const ContinuationCostParams& params = {},
    std::size_t max_candidates_per_junction = 3);

// Rendu textuel structure (meme convention que format_decomposition_report/
// format_merge_pass_report/format_overlap_report) : un candidat par bloc,
// sa description et son nombre de chemins resultants. Le detail par terme de
// cout est ajoute une fois decomposition_cost.hpp integre (§ mode
// shadow-log, plan de refonte) -- pour l'instant, diagnostic topologique
// seul.
[[nodiscard]] std::string format_decomposition_candidates_report(const DecompositionCandidateSet& candidates);

}  // namespace openstitch::satin_planning
