// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>

class QSlider;
class QLabel;

namespace openstitch::desktop {

// Réglage luminosité/contraste avec aperçu en direct : le dialogue émet
// previewRequested à chaque changement ; la fenêtre principale recalcule
// l'affichage (le calcul lui-même vit dans libs/image).
class BrightnessDialog : public QDialog {
    Q_OBJECT

public:
    explicit BrightnessDialog(QWidget* parent = nullptr);

    [[nodiscard]] double brightness() const;
    [[nodiscard]] double contrast() const;

signals:
    void previewRequested(double brightness, double contrast);

private:
    QSlider* brightness_{nullptr};
    QSlider* contrast_{nullptr};
};

}  // namespace openstitch::desktop
