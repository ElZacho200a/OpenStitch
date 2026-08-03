// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QList>
#include <QMainWindow>

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "tools.hpp"

class QGraphicsScene;
class QGraphicsItem;
class QGraphicsPathItem;
class QLabel;
class QAction;
class QListWidget;
class QDockWidget;
class QSlider;
class QTimer;
class QToolBar;
class QComboBox;
class QCheckBox;
class QVBoxLayout;
class QDoubleSpinBox;

namespace openstitch::desktop {

class CanvasView;
class PropertiesPanel;
class DocumentPanel;
class WorkflowPanel;
class EmptyStateWidget;
// Seam de test unique (déclaré ici pour le friend ci-dessous) : donne à
// MainWindowTest (tests/unit/desktop/test_main_window.cpp) accès à
// applyLoadedProject() sans exposer de méthode de chargement dans l'API
// publique de production.
class MainWindowTest;

// Fenêtre principale. Règle du projet : aucune logique métier dans les
// widgets — chargement (libs/image), placement (libs/document), transformations
// (libs/image::ops) et undo/redo (libs/commands) viennent des bibliothèques
// cœur ; cette classe câble, affiche et recalcule l'aperçu.
class MainWindow : public QMainWindow {
    Q_OBJECT
    friend class MainWindowTest;

public:
    MainWindow();
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void openImage();
    void undo();
    void redo();
    void adjustBrightnessContrast();
    void quantizeColors();
    void onCropSelected(QRectF rectMm);
    void segmentImage();
    void onCanvasClicked(QPointF posMm);
    void onCanvasContextMenu(QPointF posMm, QPoint globalPos);
    // Rectangle/ellipse dessiné (outils DrawRectangle/DrawEllipse) : interprété
    // selon `currentTool_`. Maj enfoncée + DrawEllipse = cercle contraint.
    void onBoxDrawn(QRectF rectMm, Qt::KeyboardModifiers modifiers);
    // Double-clic : clôt un polygone en cours de tracé (outil DrawPolygon).
    void onCanvasDoubleClicked(QPointF posMm);
    void deleteSelectedRegion();
    void recolorSelectedRegion();
    void vectorizeSelectedRegion();
    void autoDigitize();
    void createRunningStitchObject();
    void createTatamiObject();
    void createSatinObject();
    void autoConvertToSatin();
    void changeFillAngle();
    void convertSatinsToTatami();
    // Change le type de points d'un objet de broderie (contour/tatami/satin).
    // Le type satin exige des rails, construits depuis le contour source.
    void setStitchType(ObjectId embroideryId, int type);
    void showStatistics();
    void setHoopSize();
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
    // Bascule le mode d'édition des points générés (Lot 8.2) : entrée/sortie
    // propre (capture/relâche l'objet cible, purge l'aperçu de vue brute mis
    // en cache), jamais de mutation du document ici (une seule commande par
    // glisser, construite ailleurs — cf. renderBase).
    void onStitchEditModeToggled(bool on);
    void onSatinGuideModeToggled(bool on);
    void addSatinGuide();
    void removeSelectedSatinGuide();
    // Abandonne les retouches manuelles d'un objet (confirmation explicite,
    // DiscardOverridesCommand annulable) — appelée depuis l'inspecteur ou la
    // barre contextuelle, jamais de mutation directe hors commande.
    void discardOverrides(ObjectId id);

private:
    // Applique un projet déjà construit (charge depuis un fichier ou fixture
    // de test) : remplace le document, réinitialise undo/sélection, rafraîchit.
    void applyLoadedProject(document::Project project);
    // Objet de broderie ciblé par la sélection courante (broderie choisie
    // dans l'ordre de couture, sinon remplissage rattaché à l'objet vectoriel
    // sélectionné au canevas ; nullptr sinon). Résolution partagée par
    // updateContextToolbar/updateInspector/syncDocumentSelection.
    [[nodiscard]] document::EmbroideryObject* resolveSelectedEmbroidery();
    // Etat Clean/ManuallyEdited/Dirty (Lot 8.2) de l'objet `id`, depuis le
    // cache `editStates_` rafraîchi à chaque `refreshImage()` (Clean si absent
    // du cache : c'est l'état implicite, cf. `classify_all_edit_states`).
    // Ne recalcule jamais rien elle-même (pas de logique métier ici) — lecture
    // seule d'un résultat déjà produit par le cœur.
    [[nodiscard]] stitch_generation::ObjectEditState editStateOf(ObjectId id) const;
    void buildMenus();
    void buildHelpMenu();
    void buildMainToolbar();
    void buildContextToolbar();
    // Reconstruit la barre contextuelle selon la sélection (actions rapides).
    void updateContextToolbar();
    void buildToolPalette();
    // Change le mode d'interaction du canevas (Sélection / Déplacer / Rectangle),
    // synchronise les cases d'outils, le curseur et la barre d'état.
    void setTool(Tool tool);
    void buildAnalysisPanel();
    void buildSimulationToolbar();
    void buildOrderPanel();
    void refreshOrderPanel();
    void buildFilterPanel();
    void refreshFilterPanel();
    void buildWorkflowPanel();
    void refreshWorkflow();
    void buildDocumentPanel();
    void refreshDocumentPanel();
    // Reflète la sélection courante (broderie/région) dans le panneau Document.
    void syncDocumentSelection();
    void buildPropertiesPanel();
    // Met l'inspecteur en phase avec la sélection courante (broderie > vecteur >
    // région). Ne reconstruit le formulaire que si la sélection a changé, pour ne
    // pas interrompre une édition en cours.
    void updateInspector();
    // Un objet passe-t-il les filtres d'affichage (type, couleur, taille) ?
    [[nodiscard]] bool objectPassesFilter(const document::EmbroideryObject& object) const;
    // Aire de la région source d'un objet (mm²) ; 0 si introuvable.
    [[nodiscard]] double regionAreaMm2(const document::EmbroideryObject& object) const;
    // Index de type : 0 contour, 1 tatami, 2 satin.
    [[nodiscard]] static int stitchTypeIndex(const document::EmbroideryObject& object);
    void updateSimulationRange();
    // Remplissage tatami dont l'orientation est éditable : celui choisi dans
    // l'ordre de couture, ou à défaut le tatami rattaché à l'objet vectoriel
    // sélectionné au canevas. nullptr si aucun tatami n'est visé.
    [[nodiscard]] document::EmbroideryObject* currentFillObject();
    // Objet vectoriel visible sous un point (mm, scène). Le dernier dessiné gagne.
    [[nodiscard]] std::optional<ObjectId> objectAt(QPointF posMm) const;
    // Objet de broderie rattaché à un objet vectoriel (nullptr si aucun).
    [[nodiscard]] document::EmbroideryObject* embroideryForVector(ObjectId vectorId);
    // Centre représentatif d'un objet de broderie (pour l'estimation du coût).
    [[nodiscard]] Vec2um embroideryCentroid(const document::EmbroideryObject& object) const;
    // Création manuelle de formes (rectangle/ellipse/polygone), en écho au
    // dessin de formes de base dans les logiciels de digitalisation du
    // marché : crée un `VectorObject` autonome (sans région source) que
    // l'utilisateur convertit ensuite en objet de broderie via les actions
    // « Créer un… » existantes — aucun nouveau chemin de création côté
    // broderie, tout le pipeline aval est réutilisé tel quel.
    void addVectorPrimitive(geometry::Path path, const QString& name);
    // Suit le tracé d'un polygone en cours (poignée sur le curseur -> aperçu
    // mis à jour) ; ferme (>= 3 sommets), sinon annule silencieusement.
    void updatePolygonPreview(QPointF cursorSceneMm);
    void finishPolygon();
    void cancelPolygonDraw();
    // Miroir de ce qui précède pour le tracé à main levée (outil
    // DrawFreeform) : un point par évènement CanvasView::freeformPointMm
    // (pas de segment élastique jusqu'au curseur, contrairement au polygone —
    // chaque position déjà captée EST le tracé).
    void onFreeformPointAdded(QPointF posMm);
    void finishFreeform();
    void cancelFreeformDraw();

    void executeOp(image::ImageOp op);
    void positionEmptyState();  // centre l'accueil dans la vue
    void updateEmptyState();    // affiche l'accueil quand aucun document
    // Applique la taille du cadre du document à la vue (si elle a changé).
    void applyCanvasToView();
    void refreshImage();
    void displayImage(const image::Image& img);
    // Rendu en deux couches persistantes : la couche « base » (image, vecteurs,
    // poignées, régions) n'est reconstruite qu'à l'édition ; la couche « points »
    // est la seule reconstruite pendant la simulation.
    void renderBase(const image::Image& img);
    void renderStitches();
    void updateActions();

    document::Project project_;
    commands::UndoStack undoStack_;
    image::Image processed_;  // dernier résultat du pipeline (pour l'affichage)

    QGraphicsScene* scene_{nullptr};
    CanvasView* view_{nullptr};
    EmptyStateWidget* emptyState_{nullptr};
    QLabel* cursorLabel_{nullptr};
    QList<QGraphicsItem*> baseItems_;    // couche image/vecteurs/régions
    QList<QGraphicsItem*> stitchItems_;  // couche points (reconstruite seule en simu)
    QAction* undoAct_{nullptr};
    QAction* redoAct_{nullptr};
    QAction* cropAct_{nullptr};
    QAction* showSegAct_{nullptr};
    QAction* mergeAct_{nullptr};
    QAction* showVectorsAct_{nullptr};
    QAction* showImageAct_{nullptr};
    QAction* showStitchesAct_{nullptr};
    std::vector<QDockWidget*> panelsToRestore_;  // docks masqués par « Masquer les panneaux »
    QAction* createStitchAct_{nullptr};
    QAction* createTatamiAct_{nullptr};
    QAction* createSatinAct_{nullptr};
    QAction* autoSatinAct_{nullptr};
    QAction* fillAngleAct_{nullptr};
    QAction* convertSatinAct_{nullptr};
    QAction* statsAct_{nullptr};

    // Barres d'outils et modes d'interaction.
    QToolBar* mainToolbar_{nullptr};
    QToolBar* contextToolbar_{nullptr};
    QString contextSig_;  // signature de l'état affiché (évite les reconstructions)
    QToolBar* toolPalette_{nullptr};
    QAction* toolSelectAct_{nullptr};
    QAction* toolPanAct_{nullptr};
    QAction* toolRectAct_{nullptr};
    QAction* toolDrawRectAct_{nullptr};
    QAction* toolDrawEllipseAct_{nullptr};
    QAction* toolDrawPolygonAct_{nullptr};
    QAction* toolDrawFreeformAct_{nullptr};
    QLabel* toolLabel_{nullptr};
    Tool currentTool_{Tool::Select};

    // Polygone en cours de tracé (outil DrawPolygon) : sommets déjà posés
    // (repère modèle, µm) + aperçu élastique (détruit à la fermeture, à
    // l'annulation, ou reconstruit à chaque `renderBase`/changement d'outil).
    std::vector<Vec2um> pendingPolygonVertices_;
    QGraphicsPathItem* polygonPreviewItem_{nullptr};

    // Tracé à main levée en cours (outil DrawFreeform) : points bruts captés
    // pendant le glisser (repère modèle, µm) + aperçu (même cycle de vie que
    // le polygone). Simplifié (Douglas-Peucker) seulement à la fermeture.
    std::vector<Vec2um> pendingFreeformPoints_;
    QGraphicsPathItem* freeformPreviewItem_{nullptr};

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
    std::optional<ObjectId> selectedEmbroidery_;  // objet de broderie choisi dans l'ordre de couture
    bool mergeMode_{false};

    // Mode d'édition des points générés (Lot 8.2, cf. docs/lot8-manual-editing-design.md §6) :
    // mode exclusif, lié à UN SEUL objet capturé à l'activation
    // (`stitchEditTarget_`), jamais suivi automatiquement d'un changement de
    // sélection (sortie propre exigée -- cf. updateActions). `stitchEditView_`
    // est la vue brute/fingerprint/compteur de cet objet au moment du dernier
    // rafraîchissement : source unique pour placer les poignées ET construire
    // MoveStitchPointCommand, jamais recalculée à la main ailleurs.
    QAction* stitchEditModeAct_{nullptr};
    std::optional<ObjectId> stitchEditTarget_;
    std::optional<stitch_generation::ObjectEditView> stitchEditView_;
    // Édition paramétrique des barreaux satin : contrairement au mode 8.2,
    // ces poignées régénèrent la colonne et restent donc dans le modèle.
    QAction* satinGuideModeAct_{nullptr};
    QAction* addSatinGuideAct_{nullptr};
    QAction* removeSatinGuideAct_{nullptr};
    std::optional<ObjectId> satinGuideTarget_;
    std::optional<std::size_t> selectedSatinGuide_;
    // État Clean/ManuallyEdited/Dirty des objets retouchés (absents = Clean),
    // recalculé à chaque `refreshImage()` (cf. `classify_all_edit_states`) —
    // jamais recalculé ailleurs (panneau Document, inspecteur, barre
    // contextuelle et gating du mode d'édition en lisent tous la même copie).
    std::vector<std::pair<ObjectId, stitch_generation::ObjectEditState>> editStates_;
    // Incrémenté à chaque remplacement intégral de `project_` (nouveau
    // document, projet chargé, import DST -- jamais sur une mutation en place
    // via l'undo stack). Capturé par valeur par les commandes différées
    // (QTimer::singleShot, cf. renderBase/MoveStitchPointCommand) : si la
    // génération a changé au moment où le timer se déclenche, le document a
    // été remplacé entre-temps et la commande différée est abandonnée plutôt
    // que d'exécuter sur -- ou pire, muter -- un projet qui n'est plus celui
    // pour lequel elle a été construite.
    std::uint64_t documentGeneration_{0};

    // Analyse.
    QDockWidget* analysisDock_{nullptr};
    QListWidget* analysisList_{nullptr};
    QAction* analyzeAct_{nullptr};

    // Structure du document (Objets / Régions).
    QDockWidget* documentDock_{nullptr};
    DocumentPanel* documentPanel_{nullptr};

    // Indicateur de workflow.
    QDockWidget* workflowDock_{nullptr};
    WorkflowPanel* workflowPanel_{nullptr};

    // Inspecteur de propriétés.
    QDockWidget* propertiesDock_{nullptr};
    PropertiesPanel* propertiesPanel_{nullptr};
    int inspectedKind_{-1};        // -1 rien, 0 broderie, 1 vecteur, 2 région
    std::uint64_t inspectedId_{0};

    // Ordre de couture.
    QDockWidget* orderDock_{nullptr};
    QListWidget* orderList_{nullptr};
    QLabel* orderCostLabel_{nullptr};
    QComboBox* orderStrategyCombo_{nullptr};

    // Filtres d'affichage de la broderie.
    QDockWidget* filterDock_{nullptr};
    std::array<QCheckBox*, 3> typeChecks_{nullptr, nullptr, nullptr};  // contour/tatami/satin
    QVBoxLayout* colorFilterLayout_{nullptr};
    QDoubleSpinBox* minAreaSpin_{nullptr};
    std::array<bool, 3> showType_{true, true, true};
    std::set<std::uint32_t> hiddenColors_;  // 0xRRGGBB masqués
    double minAreaMm2_{0.0};

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
