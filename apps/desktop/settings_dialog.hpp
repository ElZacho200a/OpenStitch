// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>

#include "app_settings.hpp"

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

namespace openstitch::desktop {

// Onglet « Paramètres » : réglages d'interface et de gestes d'édition (EditSettings). Le dialogue
// ne persiste rien lui-même : l'appelant lit `settings()` à l'acceptation.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    void setSettings(const EditSettings& settings);
    [[nodiscard]] EditSettings settings() const;

private:
    QSpinBox* regionOpacityPercent_{nullptr};
    QSpinBox* longPressMs_{nullptr};
    QCheckBox* snapNodes_{nullptr};
    QDoubleSpinBox* unionGapMm_{nullptr};
    QDoubleSpinBox* knifeWidthMm_{nullptr};
    QDoubleSpinBox* knifeMinLengthMm_{nullptr};
};

} // namespace openstitch::desktop
