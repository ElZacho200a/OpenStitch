// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/core/units.hpp"

namespace openstitch::document {

// Canevas de travail : zone physique dans laquelle le motif doit tenir.
// Représente pour l'instant un cadre rectangulaire simple ; les profils de
// cadres/machines (formes, marges) viendront en Phase 16 du cahier des charges.
// Repère : origine au centre, X vers la droite, Y vers le haut.
struct Canvas {
    Micrometers width{100'000};   // 100 mm par défaut (cadre courant 10x10 cm)
    Micrometers height{100'000};
};

}  // namespace openstitch::document
