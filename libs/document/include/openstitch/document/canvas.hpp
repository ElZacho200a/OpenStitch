// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>

#include "openstitch/core/units.hpp"

namespace openstitch::document {

// Canevas de travail : zone physique dans laquelle le motif doit tenir.
// Représente pour l'instant un cadre rectangulaire simple ; les profils de
// machines (formes) viendront en Phase 16 du cahier des charges.
// Repère : origine au centre, X vers la droite, Y vers le haut.
struct Canvas {
    Micrometers width{100'000}; // 100 mm par défaut (cadre courant 10x10 cm)
    Micrometers height{100'000};
    // Marge de sécurité entre le bord du cadre et la zone réellement brodable
    // (pied presseur, collision avec le cadre) : 0 = tout le cadre est brodable
    // (projets existants). La zone brodable est le cadre réduit de cette marge
    // sur chacun de ses quatre côtés.
    Micrometers margin{0};

    // Dimensions de la zone brodable, jamais négatives.
    [[nodiscard]] Micrometers usable_width() const {
        return Micrometers{std::max(0, width.value - 2 * margin.value)};
    }
    [[nodiscard]] Micrometers usable_height() const {
        return Micrometers{std::max(0, height.value - 2 * margin.value)};
    }

    bool operator==(const Canvas&) const = default;
};

} // namespace openstitch::document
