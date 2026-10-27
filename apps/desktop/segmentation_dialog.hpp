// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>

#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/segmentation.hpp"

class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

namespace openstitch::desktop {

// Dialogue « Segmenter l'image » NON MODAL : à chaque changement de réglage (après un court délai),
// il demande un nouvel aperçu à MainWindow, qui calcule en tâche de fond et affiche la carte des
// régions sur le canevas sans toucher au document. « Segmenter » valide (flux inchangé :
// confirmation d'écrasement puis commande annulable) ; fermer le dialogue abandonne l'aperçu.
// Aucune logique métier ici : réglages, statistiques et signaux.
class SegmentationDialog : public QDialog {
    Q_OBJECT

public:
    // Chiffres de l'aperçu courant, calculés par MainWindow.
    struct Stats {
        std::size_t regions{0};
        std::size_t colors{0};
        std::size_t smallRegions{0}; // régions trop petites pour un remplissage (simple contour)
        double seconds{0.0};
    };

    // `mmPerPx` : échelle de l'image (équivalent physique de la taille minimale de région).
    SegmentationDialog(double mmPerPx, QWidget* parent = nullptr);

    [[nodiscard]] segmentation::SegmentationOptions options() const;
    [[nodiscard]] bool previewEnabled() const;

    // État du calcul d'aperçu.
    void setBusy(bool busy);
    void setStats(const Stats& stats);
    void setFailed(const QString& message);
    // Deux vues bornées en pixels, indépendantes du canevas et non destructives.
    void setSourceThumbnail(const image::Image& source);
    void setSegmentationThumbnail(const segmentation::Segmentation& segmentation);
    // Mémorise les réglages pour la prochaine ouverture (appelé au moment de valider).
    void rememberSettings() const;

signals:
    // Réglages modifiés (après un court délai) : MainWindow recalcule l'aperçu.
    void optionsChanged(const openstitch::segmentation::SegmentationOptions& options);
    void previewToggled(bool enabled);
    void applyRequested(const openstitch::segmentation::SegmentationOptions& options);
    // « Peindre les zones de détail… » : MainWindow active l'outil pinceau de masque.
    void paintDetailRequested();

private:
    void scheduleChange();
    void refreshMinSizeMm();

    double mmPerPx_{0.0};
    QSpinBox* colorsSpin_{nullptr};
    QSpinBox* minSizeSpin_{nullptr};
    QLabel* minSizeMm_{nullptr};
    QSpinBox* smoothingSpin_{nullptr};
    QSpinBox* detailSmoothingSpin_{nullptr};
    QSpinBox* detailMinSizeSpin_{nullptr};
    QCheckBox* previewCheck_{nullptr};
    QLabel* sourceThumbnail_{nullptr};
    QLabel* segmentationThumbnail_{nullptr};
    QLabel* statsLabel_{nullptr};
    QLabel* stateLabel_{nullptr};
    QPushButton* applyButton_{nullptr};
    QTimer* changeTimer_{nullptr};
};

} // namespace openstitch::desktop
