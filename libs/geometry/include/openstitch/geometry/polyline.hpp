// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Polyligne aplatie (échantillonnage dense d'un chemin, courbes comprises).
// C'est la représentation de travail pour l'échantillonnage par longueur
// d'arc : toutes les mesures de longueur y sont exactes.
struct Polyline {
    std::vector<Vec2um> points;
    bool closed{false};
};

// Aplatit un Path en polyligne : les segments entre nœuds à tangentes
// (`tan_out` du nœud i, `tan_in` du nœud i+1) sont traités comme des Béziers
// cubiques et subdivisés de façon ADAPTATIVE jusqu'à ce que l'écart à la corde
// passe sous `tolerance` (jamais un pas uniforme sur t — §46). Les segments
// sans tangente restent droits. Les points consécutifs identiques sont fusionnés.
[[nodiscard]] Polyline flatten(const Path& path, Micrometers tolerance);

// Longueurs cumulées le long d'une polyligne (taille = points.size()).
[[nodiscard]] std::vector<double> cumulative_lengths(const std::vector<Vec2um>& points);

[[nodiscard]] double polyline_length(const std::vector<Vec2um>& points);

// Point à l'abscisse curviligne s (µm) le long de la polyligne, par recherche
// binaire dans les longueurs cumulées puis interpolation locale.
[[nodiscard]] Vec2um point_at_length(const std::vector<Vec2um>& points,
                                     const std::vector<double>& cumulative, double s);

// Tangente unitaire (approx.) à l'abscisse s.
[[nodiscard]] Vec2um tangent_at_length(const std::vector<Vec2um>& points,
                                       const std::vector<double>& cumulative, double s);

// Ré-échantillonne un tronçon OUVERT (>= 2 points) par longueur d'arc, avec un
// nombre de pas ÉQUILIBRÉ : n = max(1, round(L / target)), positions
// s_i = i·L/n pour i = 0..n. Les deux extrémités sont incluses exactement ;
// pas de dernier segment résiduel minuscule (§6.2).
[[nodiscard]] std::vector<Vec2um> resample_run(const std::vector<Vec2um>& points,
                                               Micrometers target);

}  // namespace openstitch::geometry
