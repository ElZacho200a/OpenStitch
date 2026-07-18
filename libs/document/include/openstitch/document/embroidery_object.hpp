// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <string>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::document {

// Paramètres du point droit (§5.1). D'autres types (satin, tatami)
// deviendront des variantes lorsque leurs générateurs arriveront.
struct RunningStitchParams {
    Micrometers stitch_length{3'000};  // longueur cible (3 mm)
    Micrometers min_length{500};       // en dessous, les points sont fusionnés
    int repeats{1};                    // 1 simple, 2 aller-retour, 3 point triple
};

// Objet de broderie de contour : suit les chemins d'un objet vectoriel.
// La géométrie reste dans l'objet vectoriel source — la séparation
// intention/points (ADR-014) : les points sont régénérés à la demande,
// jamais stockés comme vérité dans l'objet.
struct EmbroideryObject {
    ObjectId id;
    std::string name;
    ObjectId source_vector{};           // objet vectoriel suivi
    std::array<std::uint8_t, 3> rgb{};  // couleur de fil (palette réelle : Phase 10)
    RunningStitchParams params;
    bool visible{true};
};

}  // namespace openstitch::document
