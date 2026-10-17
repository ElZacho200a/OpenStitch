// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QWidget>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "openstitch/segmentation/color_merge.hpp"

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTimer;

namespace openstitch::desktop {

// Panneau « Couleurs » de la phase de segmentation : la liste des teintes distinctes (pastille,
// part de l'image, nombre de régions) et les commandes de fusion de COULEURS -- manuelle (plusieurs
// teintes vers la teinte active) ou automatique (réduire à N couleurs, fusionner les teintes
// proches), avec aperçu avant d'appliquer. Aucune logique métier ici : présentation et signaux ;
// MainWindow calcule les plans (`segmentation::plan_*`) et les applique par une commande annulable.
class SegmentationColorsPanel : public QWidget {
    Q_OBJECT

public:
    using Rgb = std::array<std::uint8_t, 3>;

    explicit SegmentationColorsPanel(QWidget* parent = nullptr);

    // Rafraîchit la liste (la sélection est conservée par couleur) et remet la réduction
    // automatique à « aucune » : N = nombre de teintes, écart 0. `total_pixels` : base des
    // pourcentages (pixels de régions).
    void setClasses(std::vector<segmentation::ColorClass> classes, std::size_t total_pixels);

    [[nodiscard]] std::vector<Rgb> selectedColors() const;
    [[nodiscard]] bool mergeAdjacent() const;

signals:
    // Sélection de teintes (clic dans la liste) : MainWindow sélectionne leurs régions au canevas.
    void colorsSelected(const std::vector<Rgb>& colors);
    // Fusion manuelle : `sources` (teintes sélectionnées) prennent la couleur `target` (teinte
    // active).
    void mergeRequested(const std::vector<Rgb>& sources, Rgb target, bool mergeAdjacent);
    // Aperçu de la réduction automatique (après un court délai sur les réglages). `maxColors` = 0
    // et `maxDistance` = 0 n'arrivent jamais ici : c'est `reductionPreviewCleared`.
    void reductionPreviewRequested(int maxColors, double maxDistance, bool mergeAdjacent);
    void reductionPreviewCleared();
    void reductionApplyRequested(int maxColors, double maxDistance, bool mergeAdjacent);

private:
    void onSelectionChanged();
    void scheduleReductionPreview();
    void emitReductionPreview();
    [[nodiscard]] bool reductionActive() const;
    // Limite de couleurs envoyée aux signaux : 0 (sans limite) tant que N vaut le nombre de
    // teintes.
    [[nodiscard]] int maxColors() const;
    void updateButtons();

    QListWidget* list_{nullptr};
    QLabel* summary_{nullptr};
    QPushButton* mergeButton_{nullptr};
    QSpinBox* countSpin_{nullptr};
    QDoubleSpinBox* distanceSpin_{nullptr};
    QCheckBox* adjacentCheck_{nullptr};
    QPushButton* applyButton_{nullptr};
    QPushButton* cancelButton_{nullptr};
    QTimer* previewTimer_{nullptr};
    int classCount_{0};
    bool updating_{false};
};

} // namespace openstitch::desktop
