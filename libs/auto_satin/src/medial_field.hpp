// SPDX-License-Identifier: Apache-2.0
// En-tête interne à libs/auto_satin : primitive "nearest boundary feet",
// remplacement du ray-cast unique de cross_section (HP-STI-018, Phase A,
// specs/plans/hp-sti-018-turning-satin.md §2.1). Opère sur le même
// std::vector<Poly> que geometry_detail.hpp (sortie de region_polys) --
// aucune nouvelle représentation géométrique. Phase A seulement : ce header
// n'est encore câblé nulle part dans satin_column.cpp (ça viendra en Phase
// B/C avec corridor.hpp).
#pragma once

#include "geometry_detail.hpp"

#include <cstddef>
#include <vector>

namespace openstitch::auto_satin::detail {

// Point sur un contour source le plus proche d'un point intérieur interrogé,
// avec assez d'identité pour permettre un suivi par continuité entre
// stations consécutives (Phase B/C, trace_corridor) -- jamais re-choisi par
// la seule distance brute, qui ne distingue pas deux pieds presque égaux à
// un coin ou une confluence.
struct BoundaryFoot {
    P2 point;                  // PAS Vec2um -- précision double conservée pour le suivi de
                               // continuité (phases suivantes) ; conversion en Vec2um
                               // seulement aux frontières de sortie finales.
    std::size_t poly_index{0}; // index dans polys (0 = contour extérieur, 1.. = trous)
    std::size_t edge_index{0}; // arête [edge_index, edge_index+1) du polygone poly_index
    double edge_t{0.0};        // 0..1 le long de cette arête
    double distance_um{0.0};
};

struct FootQuery {
    int max_feet{3}; // 2 = corridor ordinaire ; un 3e qui arrive signale une jonction
    double tolerance_relative{0.02}; // pieds à moins de 2% du minimum = "à égalité" (multiplicité)
};

// Pied(s) de bord le(s) plus proche(s) du point intérieur `p`, parmi toutes
// les arêtes de tous les polygones de `polys`, jusqu'à `query.max_feet`,
// triés par distance croissante, après filtrage de tolérance (ne garde que
// les candidats à distance_um <= min_distance * (1 + tolerance_relative) --
// ce filtre peut donc rendre moins de max_feet résultats, c'est volontaire,
// ce n'est pas un compte fixe).
//
// Parcours UNIQUE O(E) (E = nombre total d'arêtes de `polys`) : un ensemble
// borné des meilleurs candidats (taille <= max_feet) est maintenu trié en
// cours de scan par insertion -- jamais de collecte-puis-tri complet de
// toutes les arêtes (ça serait O(E log E), explicitement signalé comme un
// risque à éviter en revue). Avec max_feet une petite constante (3 par
// défaut), le coût par arête (recherche + insertion dans un vecteur borné)
// est lui-même borné, donc le tout reste dans la même classe de complexité
// que cross_section.
//
// Départage déterministe des égalités exactes de distance : poly_index
// croissant, puis edge_index croissant, puis edge_t croissant.
//
// Polygone dégénéré (< 3 sommets) : ignoré silencieusement (aucune arête
// valable), ne doit jamais faire planter -- cohérent avec region_polys qui
// filtre déjà ce cas en amont (geometry_detail.hpp), ce garde-fou n'est donc
// utile qu'aux appels directs/tests qui construisent un Poly à la main.
//
// Sommet partagé par deux arêtes, `p` équidistant des deux : les deux pieds
// sont rapportés séparément (même `point`, `edge_index` différent), jamais
// dédupliqués -- `edge_index` fait partie de l'identité du pied précisément
// pour ne rien masquer, au lieu d'être réduit à un seul point comme le ferait
// project_to_contour (qui ne garde que le meilleur candidat global, un usage
// diagnostic différent).
//
// ATTENTION (revue Phase A, à respecter en Phase B/C) : un simple sommet
// ORDINAIRE de polygone (il en existe à CHAQUE sommet de CHAQUE contour, pas
// seulement aux vraies jonctions du squelette) peut à lui seul contribuer 2
// pieds à égalité dès qu'un point interrogé tombe près de ce sommet -- ce qui
// est routinier pour toute approximation polygonale d'une courbe. Compter les
// pieds (foot_multiplicity) ne suffit donc PAS à détecter une vraie jonction :
// un consommateur en aval DOIT aussi vérifier que les pieds comptés comme
// "multiples" ont des `point` NON coïncidents (au moins deux localisations
// distinctes), pas seulement >= 3 entrées dans le vecteur. Voir le test
// "jonction reelle a 3 branches distinctes" dans test_medial_field.cpp pour
// la distinction entre ce cas et un simple sommet.
[[nodiscard]] std::vector<BoundaryFoot> nearest_boundary_feet(const std::vector<Poly>& polys, P2 p,
                                                              const FootQuery& query = {});

} // namespace openstitch::auto_satin::detail
