// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/satin_coverage/coverage.hpp"

namespace openstitch::satin_planning {

// §14 du plan de refonte satin, suite (2026-08-14) : famille de coupes
// candidates qui ne depend d'AUCUN graphe de squelette -- cible exactement
// le trou laisse par les deux familles precedentes (normale au squelette
// depuis un evenement de detachement, JunctionSeparatorInfo), toutes deux
// ancrees sur une JONCTION du squelette. Une region SANS jonction interne
// (un seul chemin topologique -- un anneau, une entaille profonde, un
// sablier) n'a rien a quoi elles puissent s'accrocher, meme quand le
// solveur local echoue clairement (ex. `notch`/`pinch`, cf. tests) --
// `decompose_and_recurse` renoncait alors immediatement, sans jamais tenter
// la seule chose qui reste geometriquement pertinente : couper a travers
// une concavite reelle du CONTOUR lui-meme.
struct ConcavityCutParams {
    // Rejette une coupe qui produirait un fragment plus petit que ce seuil
    // (meme convention que `CutCandidateParams::min_piece_area_mm2`).
    double min_piece_area_mm2{0.3};
    Micrometers cut_width{20};
    // Distance de projection utilisee pour tester la direction "vers
    // l'interieur" a une concavite (bissectrice locale) -- purement interne
    // a la construction du candidat, sans effet sur le resultat final
    // (`geometry::cut_path_set` etend de toute facon le segment tres au-dela
    // de la region).
    double probe_distance_um{1'000'000.0};
    // Un sommet dont le virage local (test reflex) est infime (contour
    // quasi rectiligne, un pixel de bruit de tracage/segmentation) n'est PAS
    // une vraie concavite -- filtre AVANT toute construction de candidat.
    // Defaut trouve necessaire en pratique (2026-08-14, corpus reel
    // tentabrode/GISTRE) : un contour issu de vectorisation peut compter des
    // dizaines de sommets "reflex" au sens strict mais geometriquement
    // negligeables, faisant exploser combinatoirement la famille
    // concavite->concavite (O(n^2) paires) sans qu'aucun ne represente une
    // vraie encoche.
    double min_reflex_turn_deg{15.0};
    // Garde-fou de dernier recours (defense en profondeur, au-dela du
    // filtre d'angle ci-dessus) : borne dure sur le nombre de sommets reflex
    // retenus pour la famille concavite->concavite (les plus prononces
    // d'abord, par magnitude de virage) -- borne les paires a
    // max_reflex_vertices*(max_reflex_vertices-1)/2 dans le pire cas, quelle
    // que soit la complexite reelle du contour.
    std::size_t max_reflex_vertices{12};
};

// Un candidat de coupe ancre sur une ou deux concavites (sommets reflex) du
// contour EXTERIEUR de la region -- jamais sur un trou, jamais sur le
// squelette. `first_piece`/`second_piece` sont deja les DEUX morceaux
// resultants (calcules une fois a la generation, jamais recalcules a la
// selection).
struct ConcavityCutCandidate {
    Vec2um a{};
    Vec2um b{};
    bool valid{false};
    geometry::PathSet first_piece;
    geometry::PathSet second_piece;
    double first_piece_area_mm2{0.0};
    double second_piece_area_mm2{0.0};
    std::string rejection_reason;  // vide si valid
    // "concavite->concavite" ou "concavite->bord oppose" -- diagnostic
    // uniquement (format_concavity_cut_candidates).
    std::string family;
};

// Genere deux familles de candidats sur le contour EXTERIEUR de `region` :
//  - concavite->concavite : chaque PAIRE de sommets reflex, coupee en ligne
//    droite entre les deux -- le cas naturel d'une entaille en V ou d'un
//    sablier (les deux "epaules" de l'entaille SONT les deux concavites).
//  - concavite->bord oppose : chaque sommet reflex seul, coupe selon la
//    bissectrice locale des deux aretes qui s'y rejoignent, orientee
//    empiriquement VERS L'INTERIEUR de la matiere (jamais suppose depuis le
//    sens de parcours du contour, verifie point par point) -- le cas d'une
//    unique encoche profonde sans concavite en vis-a-vis.
// Chaque candidat est immediatement decoupe (`geometry::cut_path_set`) et
// filtre par les memes regles que `region_split.hpp::generate_cut_candidates`
// (exactement 2 morceaux, aucun trop petit) -- aucune nouvelle regle de
// rejet inventee ici. Ne necessite ni graphe de squelette ni jonction :
// pure geometrie de contour.
[[nodiscard]] std::vector<ConcavityCutCandidate> generate_concavity_cut_candidates(
    const geometry::PathSet& region, const ConcavityCutParams& params = {});

// Choisit, parmi les candidats VALIDES (dans l'ordre ou ils apparaissent
// dans `candidates`, jusqu'a `max_candidates_evaluated`), celui dont les
// deux morceaux resultants obtiennent la meilleure couverture moyenne
// (ponderee par aire) une fois REELLEMENT construits et mesures -- meme
// principe que le beam search phase 6 (`OracleGuidedSelector`), applique
// ici a une recherche sans graphe pour la borner, donc explicitement
// limitee en nombre d'evaluations (paires concavite->concavite = O(n^2)
// sommets reflex, potentiellement nombreux). Renvoie l'index dans
// `candidates`, ou `nullopt` si aucun candidat valide n'a pu etre construit.
[[nodiscard]] std::optional<std::size_t> select_best_concavity_cut(
    const std::vector<ConcavityCutCandidate>& candidates, const auto_satin::SatinColumnsParameters& genParams,
    const satin_coverage::SatinCoverageConfig& coverageConfig, Micrometers density,
    std::size_t max_candidates_evaluated = 6);

[[nodiscard]] std::string format_concavity_cut_candidates(const std::vector<ConcavityCutCandidate>& candidates);

}  // namespace openstitch::satin_planning
