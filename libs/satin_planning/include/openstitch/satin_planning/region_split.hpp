// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/geometry/path.hpp"
#include "openstitch/satin_planning/branch_pairing.hpp"

namespace openstitch::satin_planning {

// Un point de coupe candidat teste geometriquement. `a`/`b` sont le segment
// reellement passe a `geometry::cut_path_set` (direction = normale locale a
// la branche, a la distance `distance_from_junction_um` de la jonction).
struct CutCandidate {
    double distance_from_junction_um{0.0};
    Vec2um point{};
    Vec2um a{};
    Vec2um b{};
    bool valid{false};
    double branch_piece_area_mm2{0.0};
    double remainder_piece_area_mm2{0.0};
    std::string rejection_reason;  // vide si valid
};

// Choisit un candidat parmi ceux testes pour un evenement de detachement,
// ou aucun (`nullopt`) si aucun n'est acceptable. `piece` est le morceau
// courant sur lequel les candidats ont ete generes (permet a un selecteur
// de reconstruire lui-meme la coupe via `geometry::cut_path_set(piece,
// candidates[i].a, candidates[i].b, ...)` pour l'evaluer, ex. avec l'oracle
// phase 5 -- cf. `satin_planning::beam_search`, qui depend de ce fichier,
// jamais l'inverse : ce module reste un decoupeur de polygone pur, sans
// dependance vers la generation/mesure). Par defaut (selecteur absent),
// `split_region` retient le premier candidat valide (comportement des
// phases 3/4, le plus proche de la jonction).
using CutCandidateSelector =
    std::function<std::optional<std::size_t>(const geometry::PathSet& piece, const std::vector<CutCandidate>& candidates)>;

struct CutCandidateParams {
    // Plage de distances depuis la jonction, le long de la branche a
    // detacher, ou une coupe est tentee (§11 spec SGSD : jamais exactement
    // au pixel de jonction, rarement la meilleure frontiere physique).
    // Le maximum est volontairement large : pres d'une confluence ou
    // plusieurs branches se chevauchent geometriquement (ex. largeur de la
    // branche voisine comparable a la distance testee), une coupe trop
    // proche de la jonction peut trancher AUSSI la branche voisine sans que
    // la verification "exactement 2 morceaux" ne le detecte -- verifie
    // empiriquement sur `t` (2026-08-13) : une coupe a 300µm laisse une
    // fausse jonction residuelle dans le morceau isole, resolue seulement a
    // partir de ~2600µm.
    double search_min_um{300.0};
    double search_max_um{4000.0};
    double search_step_um{300.0};
    Micrometers cut_width{20};  // meme defaut que geometry::cut_path_set
    // Rejette une coupe qui produirait un fragment plus petit que ce seuil
    // (§13 : "piece extremement petite").
    double min_piece_area_mm2{0.3};
    // Quand la branche detachee se termine par une vraie extremite (pas une
    // autre jonction, cf. le pont d'un "H" qui a besoin de deux coupes
    // successives et resterait legitimement branche apres la premiere) :
    // reanalyse le morceau isole avec le pipeline de satinabilite complet
    // (`auto_satin::analyze_region`) et rejette la coupe si une jonction
    // residuelle subsiste -- c'est le garde-fou qui detecte le cas ci-dessus.
    bool verify_isolated_endpoint_branches{true};
    // Strategie de selection parmi les candidats valides (phase 6, recherche
    // a faisceau). Vide (par defaut) -> comportement historique des phases
    // 3/4 : le premier candidat valide (le plus proche de la jonction).
    CutCandidateSelector selector{};
};

// Genere et evalue les coupes candidates permettant de detacher la branche
// `edgeId` (incidente a `junctionNode`) du reste de `piece`. Balaie
// `search_min_um..search_max_um` par pas de `search_step_um` ; s'arrete des
// que la branche est plus courte que la distance testee. Rejette : pas
// exactement 2 morceaux (la ligne a traverse une zone sans rapport),
// fragment trop petit, ET (si `verify_isolated_endpoint_branches` et que le
// bout distal de la branche est une vraie extremite, pas une jonction) une
// jonction residuelle dans le morceau isole une fois reanalyse avec le
// pipeline de satinabilite complet -- un oracle LEGER (statut seulement, pas
// encore de generation de rails, cf. phase 5 du plan SGSD pour l'oracle
// complet). Renvoie tous les candidats testes, dans l'ordre croissant de
// distance -- `split_region` retient le premier valide, sauf si
// `params.selector` (phase 6) en choisit un autre.
[[nodiscard]] std::vector<CutCandidate> generate_cut_candidates(const geometry::PathSet& piece,
                                                                  const SkeletonGraph& graph,
                                                                  std::uint32_t junctionNode, std::uint32_t edgeId,
                                                                  const CutCandidateParams& params = {});

// Diagnostic d'un evenement de detachement (une jonction, une branche
// detachee a cette jonction) : tous les candidats testes et lequel, le cas
// echeant, a ete retenu.
struct CutAttempt {
    std::uint32_t junction{0};
    std::uint32_t edge{0};
    std::vector<CutCandidate> candidates;
    std::optional<std::size_t> selected;  // index dans candidates
};

// Sous-region reelle issue du decoupage, associee au SatinPath dont elle est
// l'empreinte geometrique (`path_index` indexe
// `DecompositionReport::paths`).
struct SatinRegion {
    std::size_t path_index{0};
    geometry::PathSet region;
    double area_mm2{0.0};
};

struct RegionSplitReport {
    std::vector<SatinRegion> regions;           // une par SatinPath isole avec succes
    std::vector<std::size_t> unresolved_paths;  // index de chemins non isoles (coupe impossible ou assignation ambigue)
    std::vector<CutAttempt> cuts;               // un par evenement de detachement traite, dans l'ordre
};

// Decoupe reellement `region` en sous-regions, une par SatinPath de
// `decomposition` (phase 3 du plan SGSD). Traite chaque evenement de
// detachement (une jonction, une arete de son `detached`) dans l'ordre
// deterministe (jonction puis arete croissantes) : localise le morceau
// courant contenant la jonction, tente une coupe, et si elle reussit
// remplace ce morceau par les deux resultants dans le pool. Une branche a
// deux bouts de jonction (ex. le pont d'un "H") est ainsi isolee par DEUX
// coupes successives sans traitement special -- le second evenement
// retrouve naturellement le morceau laisse par le premier. Assignation
// finale : chaque SatinPath recoit le morceau du pool qui contient un point
// interieur de son propre trace (un noeud strictement interne au chemin, ou
// le milieu de ses extremites s'il n'a pas de noeud interne). Une branche
// qu'aucune coupe valide ne separe reste fusionnee avec son morceau parent
// et son index est reporte dans `unresolved_paths`. Ne genere aucune
// geometrie satin : c'est un decoupage de polygone pur.
[[nodiscard]] RegionSplitReport split_region(const geometry::PathSet& region, const SkeletonGraph& graph,
                                              const DecompositionReport& decomposition,
                                              const CutCandidateParams& params = {});

// Rendu textuel structure (evenements de detachement, candidats testes,
// selection, regions resultantes) pour le debug -- meme convention que
// `format_decomposition_report`.
[[nodiscard]] std::string format_region_split_report(const RegionSplitReport& report,
                                                      const DecompositionReport& decomposition);

}  // namespace openstitch::satin_planning
