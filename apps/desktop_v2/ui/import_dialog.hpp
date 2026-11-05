// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>
#include <QString>

#include "openstitch/project_ops/factory.hpp"
#include "openstitch/segmentation/segmentation.hpp"
#include "segmentation_preview.hpp"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTimer;
class QWidget;

namespace openstitch::desktop_v2 {

// Fenêtre d'import d'une image : l'utilisateur règle la taille cible, le nombre de couleurs, la
// taille minimale des régions, le lissage et le fond, et VOIT l'image d'origine à côté de la
// segmentation qui en résultera, avant de lancer la numérisation. L'aperçu est calculé par
// l'appelant (en arrière-plan) : la fenêtre émet `previewRequested` après une courte pause et
// affiche le résultat reçu. Aucune logique métier : les réglages sont ceux de `DigitizeSettings`.
class ImportDialog final : public QDialog {
    Q_OBJECT

public:
    ImportDialog(const QString& imagePath, double defaultHeightMm, QWidget* parent = nullptr);

    [[nodiscard]] double heightMm() const;
    [[nodiscard]] project_ops::DigitizeSettings settings() const;
    [[nodiscard]] segmentation::SegmentationOptions segmentationOptions() const;

    void setPreview(const SegmentationPreview& preview);
    void setPreviewError(const QString& message);

    // Accès pour les tests et l'accessibilité.
    void setColors(int colors);
    void setMinRegionPx(int px);
    void setSmoothing(int px);
    [[nodiscard]] bool previewShown() const { return previewShown_; }
    [[nodiscard]] int paletteCount() const { return paletteCount_; }
    [[nodiscard]] QString summary() const;
    [[nodiscard]] bool sourceShown() const { return sourceShown_; }

signals:
    void previewRequested(segmentation::SegmentationOptions options);

private:
    void scheduleRequest();

    QSpinBox* colors_{nullptr};
    QSpinBox* minRegion_{nullptr};
    QSpinBox* smoothing_{nullptr};
    QDoubleSpinBox* height_{nullptr};
    QComboBox* background_{nullptr};
    QComboBox* mode_{nullptr};
    QLabel* source_{nullptr};
    QLabel* segmented_{nullptr};
    QLabel* summary_{nullptr};
    QWidget* palette_{nullptr};
    QTimer* debounce_{nullptr};
    bool previewShown_{false};
    bool sourceShown_{false};
    int paletteCount_{0};
};

} // namespace openstitch::desktop_v2
