// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>
#include <QHash>
#include <QVector>

#include <cstdint>
#include <optional>

#include "ai_preferences.hpp"
#include "openstitch/ai_segmentation/mask_result.hpp"
#include "openstitch/ai_segmentation/topology_cleanup.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/segmentation.hpp"
#include "sam_worker_client.hpp"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace openstitch::desktop {

// Boîte de dialogue « Segmenter avec l'IA » : choix du modèle/profil,
// lancement du worker SAM 2, revue des masques (garder/exclure, protéger,
// fusionner), puis nettoyage topologique produisant une
// `segmentation::Segmentation` prête pour le pipeline de vectorisation
// EXISTANT (jamais un second vectoriseur). Le dialogue ne touche jamais
// `document::Project` ni `UndoStack` — c'est à l'appelant (MainWindow, qui
// possède déjà `IdGenerator<ObjectId>`) de vectoriser le résultat et de le
// pousser via `AddObjectBatchCommand`, exactement comme pour
// l'autonumérisation classique.
class AiSegmentationDialog : public QDialog {
    Q_OBJECT

public:
    AiSegmentationDialog(image::Image sourceImage, Millimeters mmPerPx, AiPreferences prefs,
                        QWidget* parent = nullptr);
    ~AiSegmentationDialog() override;

    // Valide seulement après un clic sur « Valider » réussi.
    [[nodiscard]] bool hasResult() const { return validatedSegmentation_.has_value(); }
    [[nodiscard]] std::optional<segmentation::Segmentation> takeSegmentation();
    [[nodiscard]] const ai_segmentation::SegmentationValidationReport& validationReport() const {
        return validationReport_;
    }

private slots:
    void onAnalyzeClicked();
    void onCancelClicked();
    void onMergeClicked();
    void onValidateClicked();
    void onWorkerStateChanged(SamWorkerClient::State state);
    void onWorkerProgress(QString requestId, QString stage);
    void onModelReady(QString modelWorkerId, QString device, double loadSeconds);
    void onSegmentResult(QString requestId, QString jobDir, QString masksFile, int maskCount);
    void onWorkerError(QString requestId, openstitch::ai_segmentation::AiErrorCode code, QString message,
                       QString detail);
    void onTableSelectionChanged();

private:
    enum class Phase { Idle, StartingWorker, LoadingModel, Segmenting };

    void setupUi();
    void setStatus(const QString& text);
    void loadMasksIntoTable();
    [[nodiscard]] QVector<std::uint8_t> loadMaskPixels(const ai_segmentation::MaskEntry& entry) const;
    void updateSelectedMaskPreview();
    [[nodiscard]] QString jobDirPath() const;

    image::Image sourceImage_;
    Millimeters mmPerPx_;
    AiPreferences prefs_;
    SamWorkerClient* client_{nullptr};
    QString jobId_;
    QString activeRequestId_;
    Phase phase_{Phase::Idle};
    ai_segmentation::ModelId pendingModel_{ai_segmentation::ModelId::Small};

    ai_segmentation::MaskCollection masks_;
    QHash<int, QVector<std::uint8_t>> maskPixels_;  // mask id -> bitmap 0/1 (width*height)

    QComboBox* modelCombo_{nullptr};
    QComboBox* profileCombo_{nullptr};
    QPushButton* analyzeButton_{nullptr};
    QPushButton* cancelButton_{nullptr};
    QLabel* statusLabel_{nullptr};
    QProgressBar* progressBar_{nullptr};
    QLabel* previewLabel_{nullptr};
    QLabel* selectionPreviewLabel_{nullptr};
    QTableWidget* maskTable_{nullptr};
    QPushButton* mergeButton_{nullptr};
    QDoubleSpinBox* minIslandAreaSpin_{nullptr};
    QDoubleSpinBox* minHoleAreaSpin_{nullptr};
    // SAM 2 découpe par forme/objet, jamais par couleur (ce n'est pas un
    // réglage : ce n'est pas ce que fait ce modèle). Pour l'usage réel visé
    // ici (préparer des blocs de couleur pour la numérisation), chaque forme
    // retenue est ensuite subdivisée par couleur avec l'algorithme de
    // quantification CIELAB déjà utilisé par la segmentation classique —
    // activé par défaut, car c'est le besoin premier de cet outil.
    QCheckBox* colorRefineCheck_{nullptr};
    QSpinBox* colorRefineColorsSpin_{nullptr};
    QSpinBox* colorRefineMinSizeSpin_{nullptr};
    QDialogButtonBox* buttons_{nullptr};
    QPushButton* validateButton_{nullptr};

    std::optional<segmentation::Segmentation> validatedSegmentation_;
    ai_segmentation::SegmentationValidationReport validationReport_;
};

}  // namespace openstitch::desktop
