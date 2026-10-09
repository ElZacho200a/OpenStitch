// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QDialog>

#include "realistic_preferences.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;

namespace openstitch::desktop {

// Dialogue « Rendu réaliste » (Affichage > Rendu réaliste > Réglages…) :
// non modal, chaque modification est émise tout de suite (`preferencesChanged`)
// pour que le canevas se mette à jour en direct. Ne calcule rien lui-même :
// il édite uniquement une `RealisticPreferences` ; le rendu vit dans
// libs/stitch_render et la peinture dans MainWindow.
class RealisticRenderDialog : public QDialog {
    Q_OBJECT

public:
    explicit RealisticRenderDialog(const RealisticPreferences& initial, QWidget* parent = nullptr);

    // Valeurs actuellement affichées.
    [[nodiscard]] RealisticPreferences preferences() const;
    // Remplace les valeurs affichées sans émettre `preferencesChanged`
    // (synchronisation depuis l'extérieur : bascule du menu, restauration).
    void setPreferences(const RealisticPreferences& prefs);
    // Couleur du tissu (le sélecteur de couleur interactif appelle ceci).
    void setFabricColor(const QColor& color);

signals:
    void preferencesChanged(const openstitch::desktop::RealisticPreferences& prefs);

private:
    void emitChanged();
    void chooseFabricColor();
    void updateFabricControls();
    void updateColorSwatch();
    [[nodiscard]] static QSlider* makeSlider(QWidget* parent, const char* name);

    QCheckBox* enabled_{nullptr};
    QDoubleSpinBox* threadWidth_{nullptr};
    QSlider* relief_{nullptr};
    QSlider* sheen_{nullptr};
    QSlider* twist_{nullptr};
    QSlider* shadow_{nullptr};
    QComboBox* quality_{nullptr};
    QCheckBox* fabricOpaque_{nullptr};
    QComboBox* fabricTexture_{nullptr};
    QSlider* fabricRelief_{nullptr};
    QPushButton* fabricColorButton_{nullptr};
    QColor fabricColor_;
    bool updating_{false};
};

} // namespace openstitch::desktop
