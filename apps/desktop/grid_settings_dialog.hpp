// SPDX-License-Identifier: Apache-2.0
#pragma once

// Réglages de la grille du canevas (HP-VIEW-010) : visibilité, pas fin (ou automatique), une
// ligne majeure tous les N pas, couleur. Le dialogue ne fait que choisir des valeurs ; le calcul
// des lignes est dans libs/stitch_render (view_display.hpp).

#include <QColor>
#include <QDialog>

#include "openstitch/stitch_render/view_display.hpp"

class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;

namespace openstitch::desktop {

class GridSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit GridSettingsDialog(const stitch_render::GridSettings& initial,
                                QWidget* parent = nullptr);

    [[nodiscard]] stitch_render::GridSettings settings() const;
    // Pour les tests : choisit la couleur sans ouvrir le sélecteur.
    void setColor(const QColor& color);

private:
    void updateEnabled();
    void updateColorButton();

    QCheckBox* visible_{nullptr};
    QCheckBox* auto_{nullptr};
    QDoubleSpinBox* step_{nullptr};
    QSpinBox* major_{nullptr};
    QCheckBox* themeColor_{nullptr};
    QPushButton* colorButton_{nullptr};
    QColor color_;
};

} // namespace openstitch::desktop
