// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QList>
#include <QMainWindow>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

class QGraphicsScene;
class QLabel;
class QAction;
class QListWidget;
class QDockWidget;
class QSlider;
class QTimer;
class QToolBar;
class QComboBox;

namespace openstitch::desktop {

class CanvasView;

// Fenêtre principale. Règle du projet : aucune logique métier dans les
// widgets — chargement (libs/image), placement (libs/document), transformations
// (libs/image::ops) et undo/redo (libs/commands) viennent des bibliothèques
// cœur ; cette classe câble, affiche et recalcule l'aperçu.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

private slots:
    void openImage();
    void undo();
    void redo();
    void adjustBrightnessContrast();
    void quantizeColors();
    void onCropSelected(QRectF rectMm);
    void segmentImage();
    void onCanvasClicked(QPointF posMm);
    void deleteSelectedRegion();
    void recolorSelectedRegion();
    void vectorizeSelectedRegion();
    void autoDigitize();
    void createRunningStitchObject();
    void createTatamiObject();
    void createSatinObject();
    void showStatistics();
    void exportDst();
    void importDst();
    void saveProject();
    void loadProject();
    void runAnalysis();
    void toggleSimulation();
    void onSimSliderMoved(int value);
    void onSimTick();
    void moveObjectUp();
    void moveObjectDown();
    void toggleObjectLock();
    void applyOrderStrategy();

private:
    void buildMenus();
    void buildAnalysisPanel();
    void buildSimulationToolbar();
    void buildOrderPanel();
    void refreshOrderPanel();
    void updateSimulationRange();
    // Centre représentatif d'un objet de broderie (pour l'estimation du coût).
    [[nodiscard]] Vec2um embroideryCentroid(const document::EmbroideryObject& object) const;
    void executeOp(image::ImageOp op);
    void refreshImage();
    void displayImage(const image::Image& img);
    void updateActions();

    document::Project project_;
    commands::UndoStack undoStack_;
    image::Image processed_;  // dernier résultat du pipeline (pour l'affichage)

    QGraphicsScene* scene_{nullptr};
    CanvasView* view_{nullptr};
    QLabel* cursorLabel_{nullptr};
    QAction* undoAct_{nullptr};
    QAction* redoAct_{nullptr};
    QAction* cropAct_{nullptr};
    QAction* showSegAct_{nullptr};
    QAction* mergeAct_{nullptr};
    QAction* showVectorsAct_{nullptr};
    QAction* showStitchesAct_{nullptr};
    QAction* createStitchAct_{nullptr};
    QAction* createTatamiAct_{nullptr};
    QAction* createSatinAct_{nullptr};
    QAction* statsAct_{nullptr};
    QList<QAction*> imageActions_;
    QList<QAction*> regionActions_;  // nécessitent une région sélectionnée

    // Cache des points générés — recalculé à chaque modification du document
    // (jamais une vérité stockée, ADR-014). Exception : une séquence importée
    // d'un DST est la vérité (le DST ne contient pas d'objets, §17).
    std::optional<stitch::StitchSequence> sequence_;
    bool sequenceImported_{false};
    QAction* exportDstAct_{nullptr};

    std::optional<RegionId> selectedRegion_;
    std::optional<ObjectId> selectedObject_;
    bool mergeMode_{false};

    // Analyse.
    QDockWidget* analysisDock_{nullptr};
    QListWidget* analysisList_{nullptr};
    QAction* analyzeAct_{nullptr};

    // Ordre de couture.
    QDockWidget* orderDock_{nullptr};
    QListWidget* orderList_{nullptr};
    QLabel* orderCostLabel_{nullptr};
    QComboBox* orderStrategyCombo_{nullptr};

    // Simulation de couture. Quand active (simStep_ >= 0), l'affichage ne
    // montre les points que jusqu'à cet index.
    QToolBar* simToolbar_{nullptr};
    QSlider* simSlider_{nullptr};
    QLabel* simLabel_{nullptr};
    QAction* simPlayAct_{nullptr};
    QTimer* simTimer_{nullptr};
    int simStep_{-1};  // -1 = simulation inactive (tout affiché)

    [[nodiscard]] bool simulating() const { return simStep_ >= 0; }

    // Conversion coordonnées scène (mm) -> pixel de l'image de travail.
    [[nodiscard]] std::optional<QPoint> mmToImagePixel(QPointF mm) const;
    // Chemin Qt (scène, mm, Y vers le bas) d'un objet vectoriel.
    [[nodiscard]] static QPainterPath objectPainterPath(const document::VectorObject& object);
};

}  // namespace openstitch::desktop
