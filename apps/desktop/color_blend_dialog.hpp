// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QDialog>

#include <array>
#include <optional>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/stitch/sequence.hpp"

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QShowEvent;
class QTimer;

namespace openstitch::desktop {

class BlendStitchPreview;

// Proposition transitoire : aucune mutation du projet avant la validation.
class ColorBlendDialog : public QDialog {
    Q_OBJECT
public:
    using Rgb = std::array<std::uint8_t, 3>;
    ColorBlendDialog(Rgb background, Rgb foreground, bool canAnalyze, bool mergesZones,
                     QWidget* parent = nullptr);
    void setProposal(Rgb background, Rgb foreground, const document::DensityGradient& gradient);
    void setAnalysisMessage(const QString& message);
    [[nodiscard]] bool complementary() const;
    void setComplementary(bool complementary);
    void setEditing(bool editing);
    // Le contrôleur fournit la séquence EFFECTIVE calculée sur un instantané.
    void setStitchPreview(const stitch::StitchSequence& sequence, ObjectId background,
                          ObjectId foreground, Rgb backgroundRgb, Rgb foregroundRgb);
    void setPreviewMessage(const QString& message);
    [[nodiscard]] Rgb backgroundColor() const;
    [[nodiscard]] Rgb foregroundColor() const;
    [[nodiscard]] double angleDegrees() const;
    [[nodiscard]] Micrometers startSpacing() const;
    [[nodiscard]] Micrometers endSpacing() const;
    // Portion de l'axe (fractions de 0 à 1) où l'écart varie ; aplats en dehors.
    [[nodiscard]] document::DensityCurve curve() const;
    [[nodiscard]] double transitionStart() const;
    [[nodiscard]] double transitionEnd() const;
    // Préserve l'étendue de l'analyse tant que son angle n'est pas modifié.
    [[nodiscard]] std::optional<document::DensityGradient> proposedGradient() const;

signals:
    void analysisRequested();
    // Demande différée ; réponse synchrone du contrôleur (avec BusyIndicator).
    void previewRequested();

protected:
    void showEvent(QShowEvent* event) override;

private:
    [[nodiscard]] bool validSettings() const;
    void settingsChanged();
    void requestPreview();
    void updateExplanation();
    void updatePreviewDetails();
    QLineEdit* background_;
    QLineEdit* foreground_;
    QDoubleSpinBox* start_;
    QDoubleSpinBox* end_;
    QDoubleSpinBox* angle_;
    QDoubleSpinBox* zoneStart_;
    QDoubleSpinBox* zoneEnd_;
    QComboBox* curve_;
    QComboBox* profile_;
    QComboBox* previewLayer_;
    BlendStitchPreview* preview_;
    QLabel* explanation_;
    QLabel* profileNote_;
    QLabel* previewStats_;
    QLabel* previewLegend_;
    QLabel* message_;
    QPushButton* refresh_;
    QPushButton* accept_;
    QTimer* previewTimer_;
    bool mergesZones_;
    bool editing_{false};
    bool shown_{false};
    bool requestingPreview_{false};
    bool previewPending_{false};
    std::uint64_t settingsRevision_{0};
    std::uint64_t requestedRevision_{0};
    std::optional<document::DensityGradient> proposal_;
};

} // namespace openstitch::desktop
