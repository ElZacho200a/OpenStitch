// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Options de `directional_guides_from_region` (HP-STI-018.a).
struct DirectionalGuideOptions {
    Micrometers min_edge_length{2000}; // arêtes du squelette plus courtes : ignorées
    Micrometers resample_step{1000};   // pas de ré-échantillonnage des guides
    std::size_t max_guides{8};         // nombre maximal de guides rendus
};

// Guides ouverts (`geometry::Path::closed == false`), compatibles avec
// `DirectionalFillParams::guides`. Le guide principal (arête la plus longue) vient en premier,
// puis les branches par longueur décroissante (égalité : identifiant d'arête croissant).
struct DirectionalGuideResult {
    std::vector<geometry::Path> guides;
};

// Déduit des guides directionnels du squelette de la région (rasterisation → squelette →
// graphe, `libs/auto_satin`) : un guide par branche significative, au plus `max_guides`.
// Points hors région écartés ; une branche qui garde moins de 2 points est abandonnée.
//
// Échecs (`Error::code` stable) : `E_ROUND_SHAPE` si aucune branche n'atteint `min_edge_length`
// (forme ronde ou minuscule : guide ambigu, le remplissage radial convient mieux) ;
// `E_GUIDE_ANALYSIS` si l'analyse du squelette échoue ; `E_GUIDE_OUTSIDE` si aucun guide ne reste
// dans la région. Fonction pure et déterministe ; ne génère aucun point de couture.
[[nodiscard]] Result<DirectionalGuideResult>
directional_guides_from_region(const geometry::PathSet& region,
                               const DirectionalGuideOptions& options = {});

} // namespace openstitch::stitch_generation
