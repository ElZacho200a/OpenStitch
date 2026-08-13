// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/geometry/path.hpp"
#include "openstitch/satin_planning/branch_pairing.hpp"

namespace openstitch::satin_planning {

struct CutCandidateParams {
    // Plage de distances depuis la jonction, le long de la branche a
    // detacher, ou une coupe est tentee (§11 spec SGSD : jamais exactement
    // au pixel de jonction, rarement la meilleure frontiere physique).
    double search_min_um{300.0};
    double search_max_um{1500.0};
    double search_step_um{150.0};
    Micrometers cut_width{20};  // meme defaut que geometry::cut_path_set
    // Rejette une coupe qui produirait un fragment plus petit que ce seuil
    // (§13 : "piece extremement petite").
    double min_piece_area_mm2{0.3};
};

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

// Genere et evalue geometriquement les coupes candidates permettant de
// detacher la branche `edgeId` (incidente a `junctionNode`) du reste de
// `piece`. Balaie `search_min_um..search_max_um` par pas de
// `search_step_um` ; s'arrete des que la branche est plus courte que la
// distance testee. Purement geometrique (rejette : pas exactement 2
// morceaux -- la ligne a traverse une zone sans rapport --, fragment trop
// petit) ; AUCUN oracle Auto-Satin a ce stade (§13 partiel, cf. phase 5 du
// plan SGSD). Renvoie tous les candidats testes, dans l'ordre croissant de
// distance -- le premier valide est celui retenu par `split_region`.
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
