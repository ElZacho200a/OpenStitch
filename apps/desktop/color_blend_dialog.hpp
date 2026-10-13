// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QDialog>

#include <array>
#include <optional>

#include "openstitch/document/embroidery_object.hpp"

class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;

namespace openstitch::desktop {

// Proposition transitoire : aucune mutation du projet avant la validation.
class ColorBlendDialog : public QDialog {
    Q_OBJECT
public:
    using Rgb = std::array<std::uint8_t, 3>;
    ColorBlendDialog(Rgb background, Rgb foreground, bool canAnalyze, bool mergesZones,
                     QWidget* parent = nullptr);
    void setProposal(Rgb background, Rgb foreground, const document::DensityGradient& gradient);
    void setAnalysisMessage(const QString& message);
    [[nodiscard]] Rgb backgroundColor() const;
    [[nodiscard]] Rgb foregroundColor() const;
    [[nodiscard]] double angleDegrees() const;
    [[nodiscard]] Micrometers startSpacing() const;
    [[nodiscard]] Micrometers endSpacing() const;
    // Préserve l'étendue de l'analyse tant que son angle n'est pas modifié.
    [[nodiscard]] std::optional<document::DensityGradient> proposedGradient() const;

signals:
    void analysisRequested();

private:
    void updatePreview();
    QLineEdit* background_;
    QLineEdit* foreground_;
    QDoubleSpinBox* start_;
    QDoubleSpinBox* end_;
    QDoubleSpinBox* angle_;
    QLabel* preview_;
    QLabel* message_;
    QPushButton* accept_;
    std::optional<document::DensityGradient> proposal_;
};

} // namespace openstitch::desktop
