// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "openstitch/auto_satin/skeleton_graph.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::satin_planning {

using auto_satin::SkeletonEdge;
using auto_satin::SkeletonGraph;
using auto_satin::SkeletonNode;
using auto_satin::SkeletonNodeType;

// Poids de la fonction de cout de continuation entre deux arcs incidents a
// une meme jonction (Skeleton-Guided Satin Decomposition, phase 1 :
// appariement de branches). Aucun coefficient magique disperse ailleurs dans
// le code : tout est ici, avec des valeurs par defaut documentees.
struct ContinuationCostWeights {
    double angle{1.0};
    double width{0.6};
    double curvature{0.4};
};

struct ContinuationCostParams {
    ContinuationCostWeights weights{};
    // Longueur de centerline (depuis la jonction) utilisee pour estimer la
    // tangente et la largeur locales de chaque branche. Doit rester petite
    // devant la longueur typique d'une branche pour ne capter que le
    // comportement local pres de la jonction, pas la direction globale.
    double tangent_window_um{1'500.0};
};

// Cout de continuation entre deux arcs incidents a une meme jonction : plus
// bas = meilleure continuation naturelle. Chaque composante est exposee
// separement pour le diagnostic.
struct ContinuationCost {
    double angle_cost{0.0};      // 0 = tangentes sortantes opposees (ligne droite), 1 = memes direction (repli)
    double width_cost{0.0};      // |log(largeurA/largeurB)|, 0 = largeurs identiques
    double curvature_cost{0.0};  // 0 = tangente stable sur la fenetre, plus haut = branche qui tourne
    double total{0.0};
    bool valid{false};  // false si l'une des deux branches n'a pas de centerline exploitable
};

[[nodiscard]] ContinuationCost continuation_cost(const SkeletonGraph& graph, std::uint32_t junctionNode,
                                                  std::uint32_t edgeA, std::uint32_t edgeB,
                                                  const ContinuationCostParams& params = {});

// Une paire d'arcs candidate a une jonction, avec son cout de continuation.
struct PairCandidate {
    std::uint32_t edge_a{0};
    std::uint32_t edge_b{0};
    ContinuationCost cost{};
};

// Resolution d'une jonction : quelle paire d'arcs constitue la continuation
// principale (le "trunk"), les autres arcs incidents restant detaches
// (chacun devient l'extremite d'un SatinPath secondaire).
//
// Degre 3 : les trois appariements possibles sont testes, le moins couteux
// est retenu (spec SGSD §8, cas T/Y).
// Degre >= 4 : strategie volontairement simple et sure -- une seule
// continuation principale est retenue (la paire de cout minimal), tous les
// autres arcs deviennent secondaires. Les appariements multiples (ex :
// A<->C et B<->D simultanement) sont laisses a une phase ulterieure : ils
// peuvent produire des colonnes qui se croisent geometriquement au centre de
// la jonction, ce que la phase 1 ne verifie pas encore (aucune coupe de
// polygone n'est generee ici).
struct JunctionPairingReport {
    std::uint32_t node{0};
    std::vector<PairCandidate> candidates;      // toutes les paires evaluees, triees par cout croissant
    std::vector<std::uint32_t> selected_pair;   // 0 ou 2 edge ids
    std::vector<std::uint32_t> detached;        // arcs incidents non retenus dans la paire principale
};

// Calcule le rapport d'appariement d'une jonction unique (degre >= 2 requis
// -- un noeud de degre < 2 n'a rien a apparier et produit un rapport vide).
[[nodiscard]] JunctionPairingReport pair_branches_at_junction(const SkeletonGraph& graph,
                                                               std::uint32_t junctionNode,
                                                               const ContinuationCostParams& params = {});

// Chemin topologique maximal dans le graphe de squelette : une suite d'arcs
// mis bout a bout par les appariements retenus a chaque jonction traversee.
// Commence et finit a une extremite (Endpoint) ou au bout detache d'une
// jonction (arc non retenu dans la paire principale, spec SGSD §9).
struct SatinPath {
    std::vector<std::uint32_t> edges;  // dans l'ordre de parcours, start -> end
    std::vector<std::uint32_t> nodes;  // noeuds visites, dans l'ordre ; taille = edges.size() + 1
    Vec2um start{};
    Vec2um end{};
    double length_um{0.0};
};

struct DecompositionReport {
    std::vector<JunctionPairingReport> junctions;  // une entree par noeud Junction du graphe, tri par id croissant
    std::vector<SatinPath> paths;
};

// Decompose l'integralite du graphe de squelette en SatinPath. Ne modifie
// jamais le graphe et ne genere aucune geometrie (pas de coupe de polygone,
// pas de rails) : c'est une etape de planification topologique pure, phase 1
// du plan SGSD. Deterministe (ordre des jonctions et des chemins base sur
// les ids croissants du graphe, deja deterministe par construction).
[[nodiscard]] DecompositionReport decompose_into_paths(const SkeletonGraph& graph,
                                                        const ContinuationCostParams& params = {});

// Rendu textuel structure d'un rapport de decomposition (format de
// diagnostic SGSD : jonctions, candidats d'appariement avec leur cout,
// selection, chemins resultants). Utilise par le CLI de debug et les tests ;
// jamais de fprintf direct ailleurs dans la lib.
[[nodiscard]] std::string format_decomposition_report(const SkeletonGraph& graph,
                                                       const DecompositionReport& report);

}  // namespace openstitch::satin_planning
