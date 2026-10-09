// SPDX-License-Identifier: Apache-2.0
// En-tête interne à libs/auto_satin : primitive de CORDE du moteur d'auto-satin
// par squelette (specs/plans/satin-squelette-traversees.md, §6.3). Une corde est
// l'intervalle intérieur d'une droite orientée qui passe par un échantillon de
// l'axe ; ses deux extrémités sont les points A/B d'une traversée satin.
//
// Contrairement à l'ancien `cross_section` (un seul intervalle, échecs
// silencieux), cette primitive :
//  - renvoie TOUS les intervalles intérieurs de la droite (trous, concavités) ;
//  - applique une règle demi-ouverte aux sommets (une droite qui passe
//    exactement par un sommet ne fausse pas la parité) ;
//  - ne rejette jamais une corde pour sa largeur : la décision est laissée à
//    l'appelant, qui dispose du contexte (cellule de branche, pointes).
#pragma once

#include <optional>
#include <vector>

#include "geometry_detail.hpp"

namespace openstitch::auto_satin::detail {

// Intervalle [t_lo, t_hi] le long de la droite p + t·u (u unitaire).
struct ChordInterval {
    double t_lo{0.0};
    double t_hi{0.0};

    [[nodiscard]] double length() const { return t_hi - t_lo; }
};

// Tous les intervalles intérieurs de la droite p + t·u, triés par t croissant.
// Les polygones (extérieur et trous) sont traités en règle pair-impair. `u` doit
// être unitaire. Les intervalles de longueur nulle (droite tangente) sont omis.
[[nodiscard]] std::vector<ChordInterval> line_intervals(const std::vector<Poly>& polys, P2 p, P2 u);

// L'intervalle contenant t = 0 (le point p lui-même). Si p est hors région
// mais à au plus `tolerance_um` d'un intervalle, renvoie cet intervalle (le plus
// proche) : le squelette est calculé sur un raster, le contour sur un polygone
// vectorisé, et un échantillon peut tomber d'une fraction de pixel dehors.
// nullopt si aucun intervalle ne convient.
[[nodiscard]] std::optional<ChordInterval> chord_through(const std::vector<Poly>& polys, P2 p, P2 u,
                                                         double tolerance_um);

} // namespace openstitch::auto_satin::detail
