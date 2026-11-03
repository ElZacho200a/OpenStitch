// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Bornes appliquées à la génération (un fichier ne trouve jamais le moteur sans garde-fou).
inline constexpr Micrometers kBlanketMinSpacing{300};    // 0,3 mm
inline constexpr Micrometers kBlanketMaxSpacing{20'000}; // 20 mm
inline constexpr Micrometers kBlanketMinWidth{200};      // 0,2 mm
inline constexpr Micrometers kBlanketMaxWidth{20'000};   // 20 mm

// Points d'un blanket stitch / E-stitch ou d'un zigzag simple le long d'un chemin (HP-STI-003).
// Pur et déterministe. Le chemin est aplati, ré-échantillonné en stations équidistantes
// (pas équilibré, extrémités incluses ; une boucle fermée ne répète pas sa première station),
// puis :
//  - Blanket : base(i), dent(i), base(i) pour chaque station, d'où une ligne de base cousue
//    avec des dents perpendiculaires de longueur `width` ;
//  - Zigzag : un point par station, alternant entre les deux bords de la bande.
// Le côté (`side`) se lit dans le sens du chemin, Y vers le haut. Moins de deux points
// distincts : vecteur vide.
[[nodiscard]] std::vector<Vec2um>
blanket_stitch_points(const geometry::Path& path, const document::BlanketStitchParams& params);

} // namespace openstitch::stitch_generation
