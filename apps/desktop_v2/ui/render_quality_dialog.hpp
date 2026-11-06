// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- réglages du rendu réaliste de l'aperçu : préréglages (brouillon → photo) et
// réglages détaillés (résolution, fil, tissu). Fenêtre non modale : chaque changement est émis tout
// de suite (l'aperçu se recalcule en tâche de fond, regroupé). Aucun calcul ici.

#include <QDialog>

#include "render_settings.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;

namespace openstitch::desktop_v2 {

class RenderQualityDialog : public QDialog {
    Q_OBJECT
public:
    explicit RenderQualityDialog(QWidget* parent = nullptr);

    void setSettings(const RenderSettings& settings); // sans émettre `settingsChanged`
    [[nodiscard]] RenderSettings settings() const { return current_; }

signals:
    void settingsChanged(const openstitch::desktop_v2::RenderSettings& settings);

private:
    void apply(const RenderSettings& settings, bool emitChange);
    void refreshWidgets();
    void edited();

    RenderSettings current_;
    bool refreshing_{false};
    QComboBox* preset_{nullptr};
    QDoubleSpinBox* resolution_{nullptr};
    QDoubleSpinBox* thread_{nullptr};
    QSlider* relief_{nullptr};
    QSlider* sheen_{nullptr};
    QSlider* twist_{nullptr};
    QSlider* shadow_{nullptr};
    QPushButton* fabric_{nullptr};
    QCheckBox* weave_{nullptr};
    QSlider* fabricRelief_{nullptr};
    QCheckBox* high_{nullptr};
    QLabel* hint_{nullptr};
};

} // namespace openstitch::desktop_v2
