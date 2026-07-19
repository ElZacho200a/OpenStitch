// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <string>
#include <variant>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::document {

// Paramètres du point droit de contour (§5.1).
struct RunningStitchParams {
    Micrometers stitch_length{3'000};  // longueur cible (3 mm)
    Micrometers min_length{500};       // en dessous, les points sont fusionnés
    int repeats{1};                    // 1 simple, 2 aller-retour, 3 point triple
};

// Paramètres du remplissage tatami (§5.4).
struct TatamiParams {
    Angle angle{0.0};                  // orientation des rangées (radians)
    Micrometers row_spacing{400};      // écart entre rangées (densité) — 0,4 mm
    Micrometers stitch_length{3'000};  // longueur de point le long d'une rangée
    Micrometers inset{200};            // retrait du bord (compensation de contour)
    int stagger{2};                    // rangées avant répétition de la phase des pénétrations
};

// Un objet de broderie porte un TYPE de point sous forme de variant. Chaque
// type suit la géométrie d'un objet vectoriel source (contour pour running,
// région pleine pour tatami). La séparation intention/points (ADR-014) tient :
// les points sont régénérés à la demande, jamais stockés comme vérité.
using StitchParams = std::variant<RunningStitchParams, TatamiParams>;

struct EmbroideryObject {
    ObjectId id;
    std::string name;
    ObjectId source_vector{};           // objet vectoriel suivi
    std::array<std::uint8_t, 3> rgb{};  // couleur de fil (palette réelle : Phase 10)
    StitchParams params{RunningStitchParams{}};
    bool visible{true};

    [[nodiscard]] bool is_tatami() const {
        return std::holds_alternative<TatamiParams>(params);
    }
};

}  // namespace openstitch::document
