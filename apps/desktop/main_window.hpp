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
    void createRunningStitchObject();
    void createTatamiObject();
    void createSatinObject();
    void showStatistics();
    void exportDst();
    void importDst();
    void saveProject();
    void loadProject();

private:
    void buildMenus();
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

    // Conversion coordonnées scène (mm) -> pixel de l'image de travail.
    [[nodiscard]] std::optional<QPoint> mmToImagePixel(QPointF mm) const;
    // Chemin Qt (scène, mm, Y vers le bas) d'un objet vectoriel.
    [[nodiscard]] static QPainterPath objectPainterPath(const document::VectorObject& object);
};

}  // namespace openstitch::desktop
