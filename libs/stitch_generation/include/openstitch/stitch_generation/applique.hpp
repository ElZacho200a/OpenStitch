// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Bornes appliquées à la génération (HP-SPEC-001) : le fichier n'est jamais cru sur parole.
inline constexpr Micrometers kAppliqueMinPlacement{1'000}; // 1 mm
inline constexpr Micrometers kAppliqueMaxPlacement{7'000}; // 7 mm
inline constexpr Micrometers kAppliqueMinCover{500};       // 0,5 mm
inline constexpr Micrometers kAppliqueMaxCover{20'000};    // 20 mm
inline constexpr Micrometers kAppliqueMinDensity{200};     // 0,2 mm
inline constexpr Micrometers kAppliqueMaxDensity{1'500};   // 1,5 mm
inline constexpr Micrometers kAppliqueMaxCutOffset{5'000}; // +-5 mm

// Paramètres ramenés dans leurs plages valides.
[[nodiscard]] document::AppliqueParams clamp_applique(const document::AppliqueParams& params);

// Ligne de placement : point droit le long d'un anneau (refermé s'il est fermé).
[[nodiscard]] std::vector<Vec2um> applique_placement_points(const geometry::Path& ring,
                                                            const document::AppliqueParams& params);

// Point de maintien (tack-down) le long d'un anneau : point droit, zigzag ou feston centré
// sur le contour (il traverse le bord du tissu posé).
[[nodiscard]] std::vector<Vec2um> applique_tack_points(const geometry::Path& ring,
                                                       const document::AppliqueParams& params);

// Contour de découpe du tissu : anneaux fermés (extérieurs puis trous de chaque région), aplatis
// en polylignes, décalés de `cut_offset` (>0 vers l'extérieur). Déterministe.
[[nodiscard]] std::vector<geometry::Path>
applique_cut_paths(const std::vector<geometry::PathSet>& regions,
                   const document::AppliqueParams& params);

} // namespace openstitch::stitch_generation
