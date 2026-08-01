// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/vector_object.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::autodigitize {

struct AutoOptions {
    Millimeters mm_per_px{25.4 / 96.0};
    Micrometers simplify_tolerance{200};
    // Largeur moyenne max pour proposer un satin (au-delà : tatami).
    Micrometers satin_max_width{6'000};
    // Aire min (mm²) pour un remplissage ; en dessous, simple contour.
    double min_fill_area_mm2{4.0};
    // Ignore la plus grande région (souvent le fond).
    bool skip_largest_region{false};
    // Moteur topologique : squelette, decomposition en sections ouvertes,
    // barreaux et routage par source commune. Active par defaut.
    bool use_auto_satin{true};
    // Satin automatique NAÏF (rails_from_contour) : désactivé par défaut. Ses
    // deux rails « bouts les plus éloignés » débordent sur les formes concaves
    // ou branchues (les rungs enjambent les creux). Tant que le vrai moteur
    // auto-satin (squelette) ne génère pas la géométrie, on remplit ces zones
    // en tatami — découpé proprement sur la région, donc sans débordement.
    bool use_naive_satin{false};
};

// Objets produits par l'autonumérisation : toujours ÉDITABLES (§13). Le type
// de chaque objet est choisi par la forme de sa région (satin pour les bandes
// fines, tatami pour les zones pleines, contour pour les petites régions).
struct AutoResult {
    std::vector<document::VectorObject> vectors;
    std::vector<document::EmbroideryObject> embroideries;
};

// Construit les objets à partir d'une segmentation. Les identifiants sont
// alloués via `ids` (le générateur du document), donc jamais réutilisés.
[[nodiscard]] Result<AutoResult> auto_digitize(const segmentation::Segmentation& seg,
                                               IdGenerator<ObjectId>& ids,
                                               const AutoOptions& options);

}  // namespace openstitch::autodigitize
