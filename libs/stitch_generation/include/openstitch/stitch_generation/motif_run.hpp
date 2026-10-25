// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Bornes appliquées à la génération (un fichier ne trouve jamais le moteur sans garde-fou).
inline constexpr Micrometers kMotifMinSize{500};        // 0,5 mm
inline constexpr Micrometers kMotifMaxSize{40'000};     // 40 mm
inline constexpr Micrometers kMotifMinSpacing{1'000};   // 1 mm
inline constexpr Micrometers kMotifMaxSpacing{100'000}; // 100 mm
inline constexpr Micrometers kMotifMinStitch{1'000};    // 1 mm
inline constexpr Micrometers kMotifMaxStitch{7'000};    // 7 mm

// Point d'un motif normalisé : x dans [0 ; 1000] le long du tracé (0 = début de la répétition,
// 1000 = fin, qui est aussi le début de la suivante), y dans [-500 ; 500] perpendiculairement
// (positif = à gauche du sens du tracé, Y vers le haut). Chaque motif commence en (0, 0) et finit
// en (1000, 0) pour s'enchaîner. En remplissage, le motif est centré sur (500, 0) et occupe le
// carré [0 ; 1000] x [-500 ; 500].
struct MotifPoint {
    int x{};
    int y{};
};

// Bibliothèque de base (données propres au projet, aucune source tierce).
[[nodiscard]] std::span<const MotifPoint> motif_pattern(document::MotifId id);

// Point d'un motif mis à l'échelle (µm, en double : repère local, pas une coordonnée document).
struct MotifLocal {
    double u{}; // le long de l'axe x du motif
    double v{}; // perpendiculaire
};

// Motif mis à l'échelle (`width_um` pour x in [0;1000], `height_um` pour y in [-500;500]) et
// subdivisé en pas d'au plus `max_um`. Déterministe.
[[nodiscard]] std::vector<MotifLocal> motif_local_points(document::MotifId id, double width_um,
                                                         double height_um, double max_um);

// Impose la longueur minimale `min_um` : un point plus proche que `min_um` du dernier conservé
// est retiré, sauf le dernier point du tracé.
[[nodiscard]] std::vector<Vec2um> enforce_min_length(const std::vector<Vec2um>& pts, double min_um);

// Points d'un motif répété le long d'un chemin (HP-STI-002). Le chemin est aplati et découpé en
// n = max(1, round(L / spacing)) répétitions de longueur égale (une boucle fermée referme
// exactement) ; chaque répétition suit la tangente, `alternate` retourne une sur deux.
// Pur et déterministe. Moins de deux points distincts : vecteur vide.
[[nodiscard]] std::vector<Vec2um> motif_run_points(const geometry::Path& path,
                                                   const document::MotifRunParams& params);

} // namespace openstitch::stitch_generation
