// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <string>
#include <variant>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

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

// Barreau (rung) : segment transversal reliant les deux rails, qui les découpe
// en intervalles correspondants. Produit par l'auto-satin (squelette) ; stocké
// dans le document (éditable, sérialisé), pas seulement utilisé à la génération.
struct SatinRung {
    Vec2um a{};  // côté rail A
    Vec2um b{};  // côté rail B
    constexpr bool operator==(const SatinRung&) const = default;
};

// Paramètres d'une colonne satin (§5.3). Contrairement aux autres types, le
// satin porte sa propre géométrie (deux rails éditables), car il ne se déduit
// pas d'un simple contour — il naît d'un contour découpé ou de deux chemins.
struct SatinParams {
    geometry::Path rail_a;
    geometry::Path rail_b;
    std::vector<SatinRung> rungs;  // barreaux (vide = satin manuel/legacy)
    Micrometers density{400};
    Micrometers pull_compensation{0};
    bool center_underlay{true};
    Micrometers max_width{9'000};  // au-delà, avertissement (satin trop large -> tatami)
};

// Un objet de broderie porte un TYPE de point sous forme de variant. Chaque
// type suit la géométrie d'un objet vectoriel source (contour pour running,
// région pleine pour tatami) ou porte la sienne (satin). La séparation
// intention/points (ADR-014) tient : les points sont régénérés à la demande.
using StitchParams = std::variant<RunningStitchParams, TatamiParams, SatinParams>;

struct EmbroideryObject {
    ObjectId id;
    std::string name;
    ObjectId source_vector{};           // objet vectoriel suivi
    std::array<std::uint8_t, 3> rgb{};  // couleur de fil (palette réelle : Phase 10)
    StitchParams params{RunningStitchParams{}};
    bool visible{true};
    bool locked{false};  // l'optimisation d'ordre ne déplace pas un objet verrouillé

    [[nodiscard]] bool is_tatami() const {
        return std::holds_alternative<TatamiParams>(params);
    }
    [[nodiscard]] bool is_satin() const {
        return std::holds_alternative<SatinParams>(params);
    }
};

}  // namespace openstitch::document
