// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- fenêtre principale (« Application Shell »). Elle CÂBLE et AFFICHE ; elle ne calcule
// rien : le document vit dans DocumentSession, la sélection dans SelectionModel, les calculs longs
// dans TaskManager, les outils dans ToolRegistry. Les widgets s'abonnent à ces modèles au lieu de
// se synchroniser entre eux (principe I : une sélection, une source de vérité).

#include <QMainWindow>
#include <QPointF>
#include <QRectF>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "action_catalog.hpp"
#include "arrange_batch.hpp"
#include "command_catalog.hpp"
#include "diagnostics_service.hpp"
#include "digitize_pipeline.hpp"
#include "document_services.hpp"
#include "document_session.hpp"
#include "edit_batch.hpp"
#include "image_prep.hpp"
#include "point_edit.hpp"
#include "preview_service.hpp"
#include "production_sheet.hpp"
#include "satin_column.hpp"
#include "shape_batch.hpp"
#include "source_image_tools.hpp"
#include "task_manager.hpp"
#include "tool_registry.hpp"
#include "view_tools.hpp"
#include "workspace_state.hpp"

class QAction;
class QCloseEvent;
class QActionGroup;
class QComboBox;
class QDockWidget;
class QSpinBox;
class QMenu;
class QSlider;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QTimer;
class QToolBar;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace openstitch::desktop_v2 {

class PreviewCanvas;
class InspectorPanel;
class DiagnosticsPanel;
class CalquePanel;
class ThreadPanel;

class ShellWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ShellWindow(QWidget* parent = nullptr);
    ~ShellWindow() override;

    // --- Actions de haut niveau (menus, ligne de commande, tests) ---------------------------
    // Numérise une image (tâche d'arrière-plan). `heightMm` <= 0 : résolution native.
    void openImage(const std::filesystem::path& image, double heightMm);
    void openProject(const std::filesystem::path& osp);
    bool saveProject(const std::filesystem::path& osp);
    // Dossier d'état utilisateur : liste des récents + sauvegarde automatique (toutes les
    // 60 s si le document est modifié). Sans appel, rien n'est écrit (tests, mode recette).
    void setStateDirectory(const std::filesystem::path& dir);
    // Écrit la sauvegarde automatique maintenant si le document est ouvert et modifié.
    bool autosaveNow();
    [[nodiscard]] bool hasRecovery() const { return autosave_ && autosave_->exists(); }
    // Charge la sauvegarde automatique (document non enregistré) ; faux si absente / illisible.
    bool recoverAutosave();
    void discardRecovery();
    [[nodiscard]] const std::vector<std::string>& recentFiles() const { return recent_; }
    // Calcule la séquence EFFECTIVE du document courant puis écrit le DST, en tâche annulable.
    void exportDst(const std::filesystem::path& dst);

    // Commandes (palette Ctrl+K, menus, tests) : un identifiant stable par intention.
    [[nodiscard]] std::vector<CommandEntry> commandEntries() const;
    bool runCommand(const QString& id); // faux si inconnue ou désactivée
    void showCommandPalette();
    void selectAllObjects();
    bool placeSelection(double xMm, double yMm, double widthMm, double heightMm, double rotateDeg);
    void showPlaceDialog();
    // Prétraitement de l'image source (menu Image) : une étape annulable par appel.
    bool applyPrep(const PrepRequest& request);
    void runImagePrep(const QString& id);
    // Fiche de production (PDF A4) : dimensions, comptes, plan de fils et aperçu. Tâche annulable.
    void exportProductionSheet(const std::filesystem::path& pdf);
    // Texte d'aide (raccourcis et commandes) construit depuis le registre : une seule source.
    [[nodiscard]] QString
    helpText() const; // position, taille et rotation numériques de la sélection

    // --- Image source : clic-pour-broder et zones de détail -----------------------------------
    void setSourceVisible(bool visible); // calque semi-transparent sous les fils
    [[nodiscard]] bool sourceVisible() const { return sourceShown_; }
    void setClickToStitch(int tolerance, ClickKind kind);
    void setBrushRadiusMm(double mm);
    [[nodiscard]] int clickTolerance() const { return clickTol_; }

    // --- Retouche de points (outil « stitch.points », un seul objet sélectionné) --------------
    [[nodiscard]] int selectedPoint() const {
        return pointSel_;
    } // indice dans les poignées, -1 = aucun
    bool deleteSelectedPoint();
    bool discardPointEdits();

    // Préréglages de points : enregistrés dans le projet (.osp), appliqués à la sélection.
    bool savePresetNamed(const QString& name); // réglages de l'objet principal de la sélection
    bool applyPresetIndex(int index);
    bool removePresetIndex(int index);

    // --- Affichage, simulation, contexte -------------------------------------------------------
    void setGridSpacing(double mm); // 0 = pas de grille
    [[nodiscard]] double gridSpacing() const { return gridSpacingMm_; }
    void setThemeMode(const QString& mode); // "system" | "light" | "dark"
    [[nodiscard]] QString themeMode() const { return themeMode_; }
    // Simulation de couture : `position` = nombre de commandes cousues ; >= total = tout. Marche
    // seulement avec un aperçu affiché (le total vient de la séquence complète).
    void setSimulationPosition(int position);
    [[nodiscard]] int simulationPosition() const;
    [[nodiscard]] int simulationTotal() const;
    [[nodiscard]] bool simulating() const { return simLimit_.has_value(); }
    // Menu contextuel du canevas : commandes de sélection actives (appartient à la fenêtre).
    [[nodiscard]] QMenu* buildContextMenu();
    void showContextMenu(const QPoint& globalPos, QPointF scenePx);
    void exportImage(const std::filesystem::path& png, double pxPerMm = 6.0);
    // Action du catalogue (`actionCatalog`) : avec dialogue de réglages, ou directement avec des
    // valeurs (tests, scripts). Faux si inconnue, désactivée, valeurs refusées ou lot refusé par le
    // moteur.
    void runAction(const QString& id);
    bool runActionWith(const QString& id, const Values& values);
    void newProject(); // projet vierge à cadre par défaut : point de départ du dessin

    void setCompactToolRail(bool compact);
    [[nodiscard]] bool compactToolRail() const { return compactTools_; }
    void setWorkspace(Workspace workspace);
    [[nodiscard]] Workspace workspace() const { return workspace_; }
    void waitForIdle(); // bloque jusqu'à la fin des tâches et livre leurs résultats (tests)

    [[nodiscard]] DocumentSession& session() { return session_; }
    [[nodiscard]] TaskManager& tasks() { return tasks_; }
    [[nodiscard]] const ToolRegistry& tools() const { return registry_; }
    [[nodiscard]] QString lastNotice() const;
    [[nodiscard]] QString activeToolId() const { return activeTool_; }
    [[nodiscard]] bool previewShown() const;
    [[nodiscard]] PreviewCanvas* canvas() const { return canvas_; }
    [[nodiscard]] InspectorPanel* inspector() const { return inspector_; }
    [[nodiscard]] DiagnosticsPanel* diagnostics() const { return diagnostics_; }
    [[nodiscard]] CalquePanel* calquePanel() const { return calques_; }
    [[nodiscard]] ThreadPanel* threadPanel() const { return threads_; }
    // Repère mm <-> pixels et index de sélection de l'aperçu affiché (tests, diagnostics).
    [[nodiscard]] const PreviewImage& previewFrame() const { return frame_; }
    // Un point (pixels de l'image d'aperçu) qui appartient à l'objet : pour piloter le canevas en
    // test.
    [[nodiscard]] std::optional<QPointF> objectCentrePx(std::uint64_t id) const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    bool confirmDiscardOrSave();
    bool saveCurrentProject();
    void cancelPendingDocumentLoad();
    void refreshWindowTitle();
    void buildMenus();
    void buildWorkspaceBar();
    void buildToolRail();
    void buildInspector();
    void buildStatusArea();
    void rebuildToolRail();
    void rebuildObjectList();
    void syncTreeFromSelection();
    void onSelectionChangedInTree();
    void onDocumentChanged(ChangeKind kind);
    void requestPreview();
    void setNotice(const QString& text, bool error);
    void refreshActionStates();
    void refreshTaskStatus();
    void chooseAndOpen();
    void updateSelectionBoxes();
    void onCanvasRect(QRectF scenePx, Qt::KeyboardModifiers modifiers);
    void applyToolToCanvas();
    void updateSourceOverlay();
    void updatePointMarkers();
    [[nodiscard]] std::optional<std::uint64_t> pointTarget() const;
    const image::Image* ensureProcessed();
    void onStroke(QVector<QPointF> scenePx, Qt::KeyboardModifiers modifiers);
    void rebuildPresetMenu();
    void onMeasured(QPointF a, QPointF b);
    void onKnife(QPointF a, QPointF b);
    void updateGrid();
    void refreshSimulationBar();
    void runDiagnostics();
    void onFindingActivated(const project_ops::Finding& finding);
    [[nodiscard]] PointMm selectionCentreModel() const;
    void editSelection(const QString& command); // delete / duplicate / rotate / mirror / ordre
    void nudgeSelection(int stepX, int stepUp, bool big);
    [[nodiscard]] std::optional<std::uint64_t> pickAtPx(QPointF scenePx) const;
    void onMoveStart(QPointF scenePx);
    void onMoveDragged(QPointF deltaPx);
    void onScaleDragged(QPointF pivotPx, double factor);
    void applyTransform(const std::string& batch, const QString& label);
    void createShape(const std::string& batch, const QString& label);
    void onBoxDrawn(QRectF scenePx, Qt::KeyboardModifiers modifiers);
    void onPolygonDrawn(QVector<QPointF> scenePx);
    void onCanvasClick(QPointF scenePx, Qt::KeyboardModifiers modifiers);

    DocumentSession session_;
    TaskManager tasks_;
    ToolRegistry registry_;
    Workspace workspace_{Workspace::Design};
    QString activeTool_{QStringLiteral("select")};

    PreviewCanvas* canvas_{nullptr};
    QToolBar* workspaceBar_{nullptr};
    QToolBar* toolRail_{nullptr};
    QAction* compactToolsAction_{nullptr};
    bool compactTools_{false};
    QActionGroup* workspaceGroup_{nullptr};
    QActionGroup* toolGroup_{nullptr};
    std::vector<QAction*> workspaceActions_;
    QDoubleSpinBox* heightSpin_{nullptr};
    QTreeWidget* objectTree_{nullptr};
    QLabel* summary_{nullptr};
    QLabel* notice_{nullptr};
    QLabel* activeToolStatus_{nullptr};
    QProgressBar* progress_{nullptr};
    QToolButton* cancelButton_{nullptr};
    QAction* undoAction_{nullptr};
    QAction* redoAction_{nullptr};
    QAction* saveAction_{nullptr};
    QAction* exportAction_{nullptr};
    QTimer* pump_{nullptr};
    QTimer* previewDebounce_{nullptr};
    InspectorPanel* inspector_{nullptr};
    QToolBar* simBar_{nullptr};
    QToolBar* imageBar_{nullptr};
    PointView pointView_;
    std::vector<std::size_t> pointHandles_; // poignées -> indice dans pointView_.points
    std::uint64_t pointObject_{0};
    int pointSel_{-1};
    QSpinBox* tolSpin_{nullptr};
    QComboBox* kindCombo_{nullptr};
    image::Image processed_;
    std::uint64_t processedRevision_{0};
    bool processedValid_{false};
    bool sourceShown_{false};
    int clickTol_{20};
    ClickKind clickKind_{ClickKind::Auto};
    double brushMm_{3.0};
    QSlider* simSlider_{nullptr};
    QLabel* simLabel_{nullptr};
    QTimer* simTimer_{nullptr};
    std::optional<std::size_t> simLimit_;
    double gridSpacingMm_{0.0};
    QString themeMode_{QStringLiteral("system")};
    DiagnosticsPanel* diagnostics_{nullptr};
    CalquePanel* calques_{nullptr};
    ThreadPanel* threads_{nullptr};
    QMenu* presetMenu_{nullptr};
    QDockWidget* docDock_{nullptr};
    QDockWidget* diagDock_{nullptr};
    PreviewImage frame_; // repère mm/pixels et index de sélection de l'aperçu affiché

    DocumentSession::Token sessionToken_{0};
    SelectionModel::Token selectionToken_{0};
    bool syncingTree_{false};
    TaskId previewTask_{0};
    TaskId documentLoadTask_{0};
    std::uint64_t loadGeneration_{0};
    std::uint64_t previewRevision_{0};
    bool previewShown_{false};
    bool documentOpen_{false}; // un projet est ouvert (même vide) : l'aperçu montre son cadre
    std::filesystem::path currentPath_;
    std::filesystem::path stateDir_;
    std::vector<std::string> recent_;
    std::unique_ptr<AutosaveStore> autosave_;
    QTimer* autosaveTimer_{nullptr};
    QMenu* recentMenu_{nullptr};
    QMenu* historyMenu_{nullptr};
    void noteRecent(const std::filesystem::path& osp);
    void rebuildRecentMenu();
    void rebuildHistoryMenu();
};

} // namespace openstitch::desktop_v2
