// SPDX-License-Identifier: Apache-2.0
// Éléments privés partagés entre main_window.cpp et les main_window_*.cpp (HP-QA-001) :
// petites fonctions libres et constantes auparavant locales à main_window.cpp. Aucun
// code applicatif ici ; ne pas inclure hors de apps/desktop/main_window*.cpp.
#pragma once

#include <QMessageBox>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "main_window.hpp"

namespace openstitch::desktop {

// Mode d'édition des points générés (Lot 8.2) : au-delà de ce nombre de
// points déplaçables (is_movable_point), aucune poignée n'est proposée --
// un GraphicsItem par poignée coûte trop cher sur un motif dense. Partagé
// entre renderBase (rendu) et updateActions (gating/sortie de mode + message).
inline constexpr std::size_t kMaxEditableStitchHandles = 2000;

// Repère scène (mm, Y vers le bas, Qt) <-> repère modèle (µm, Y vers le haut,
// ADR-003). Définis dans main_window.cpp.
Vec2um sceneMmToModel(QPointF sceneMm);
QPointF modelToSceneMm(Vec2um pos);

// Définie dans main_window.cpp.
Micrometers vectorize_tolerance_from_detail(int detail);

// Erreur montrable à l'utilisateur : un titre qui dit ce qui a échoué, une phrase d'explication
// et le message technique du moteur en détail repliable (jamais comme seul texte).
inline void showFriendlyError(QWidget* parent, const QString& title, const QString& explanation,
                              const std::string& technical) {
    QMessageBox box(QMessageBox::Warning, title, explanation, QMessageBox::Ok, parent);
    if (!technical.empty()) {
        box.setDetailedText(QString::fromStdString(technical));
    }
    box.exec();
}

// Famille du filtre d'affichage (0 contour, 1 remplissage, 2 satin) d'un index de type de points.
inline int stitchFilterFamily(int typeIndex) {
    switch (typeIndex) {
    case 3:
    case 6:  // remplissage à motif (HP-STI-006)
    case 7:  // concentrique
    case 8:  // radial
    case 9:  // spirale
    case 10: // croisé
    case 11: // point de croix (HP-STI-012)
        return 1;
    case 4:
    case 5: // blanket, motif le long du contour (HP-STI-002)
        return 0;
    default:
        return typeIndex;
    }
}

// Clé 0xRRGGBB des filtres de couleur d'affichage.
inline std::uint32_t colorKeyOf(const std::array<std::uint8_t, 3>& rgb) {
    return (static_cast<std::uint32_t>(rgb[0]) << 16) | (static_cast<std::uint32_t>(rgb[1]) << 8) |
           static_cast<std::uint32_t>(rgb[2]);
}

} // namespace openstitch::desktop
