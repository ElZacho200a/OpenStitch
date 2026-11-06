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
#include <functional>

#include <QPointer>
#include <memory>
#include <optional>
#include <vector>

#include "action_catalog.hpp"
#include "arrange_batch.hpp"
#include "command_catalog.hpp"
#include "companion_shared.hpp"
#include "density_service.hpp"
#include "diagnostics_service.hpp"
#include "digitize_pipeline.hpp"
#include "document_services.hpp"
#include "document_session.hpp"
#include "edit_batch.hpp"
#include "fabric_risk.hpp"
#include "fade_axis.hpp"
#include "image_prep.hpp"
#include "import_dialog.hpp"
#include "live_companion.hpp"
#include "node_edit.hpp"
#include "openstitch/agent/agent.hpp"
#include "path_optimizer.hpp"
#include "pen_shape.hpp"
#include "point_edit.hpp"
#include "preferences.hpp"
#include "preview_service.hpp"
#include "production_sheet.hpp"
#include "render_settings.hpp"
#include "satin_column.hpp"
#include "shape_batch.hpp"
#include "snap.hpp"
#include "source_image_tools.hpp"
#include "task_manager.hpp"
#include "tool_registry.hpp"
#include "view_tools.hpp"
#include "workflow_guide.hpp"
#include "workspace_state.hpp"

class QAction;
class QCloseEvent;
class QKeyEvent;
class QActionGroup;
class QComboBox;
class QDockWidget;
class QSpinBox;
class QMenu;
class QSlider;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QProgressBar;
class QTimer;
class QToolBar;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace openstitch::desktop_v2 {

class PreviewCanvas;
class WelcomePage;
class InspectorPanel;
class DiagnosticsPanel;
class CalquePanel;
class ThreadPanel;
class CompanionPanel;
class OptimizePanel;

class NodeEditController;
class RegionEditController;
class RegionPanel;
class AnalysisOverlays;
class LiveServer;
class ChatPanel;
class FeedbackCue;

class ShellWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ShellWindow(QWidget* parent = nullptr);
    ~ShellWindow() override;

    // --- Actions de haut niveau (menus, ligne de commande, tests) ---------------------------
    // Numérise une image (tâche d'arrière-plan). `heightMm` <= 0 : résolution native.
    void openImage(const std::filesystem::path& image, double heightMm,
                   const project_ops::DigitizeSettings& settings = {});
    // Ouvre la fenêtre d'import (réglages + segmentation en aperçu) puis numérise à la validation.
    void importImage(const std::filesystem::path& image);
    // Magnétisme du déplacement d'objets (bords, centres, axe du motif) : actif par défaut,
    // suspendu tant qu'Alt est maintenu.
    void setSnapEnabled(bool on) { snapEnabled_ = on; }
    [[nodiscard]] bool snapEnabled() const { return snapEnabled_; }
    [[nodiscard]] ImportDialog* importDialog() const { return importDialog_; }
    void openProject(const std::filesystem::path& osp);
    // « Enregistrer sous » : demande toujours un chemin (extension .osp) ; le projet courant prend
    // ce chemin. `exportAgain` : réécrit le dernier fichier machine avec les mêmes réglages.
    bool saveProjectAs();
    bool exportAgain();
    // Broderie machine (DST, PES, JEF, EXP… tout ce que le registre sait lire) : nouveau projet,
    // design centré dans le cadre, une seule étape de départ. DXF : tracés ajoutés comme objets
    // vectoriels (annulable) ; export : tous les contours vectoriels.
    void importMachineDesign(const std::filesystem::path& file);
    void importDxfFile(const std::filesystem::path& file);
    void exportDxfFile(const std::filesystem::path& file);
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
    // Retouche de points en sélection multiple (Ctrl/Maj+clic, Ctrl+A, Échap).
    [[nodiscard]] std::vector<std::size_t> selectedPointIndices() const;
    void selectAllPoints();
    void clearPointSelection();
    bool restoreSelectedPoints();
    bool setSelectedPointsJump(bool jump);
    bool setSelectedPointsTrim(bool trim);
    [[nodiscard]] int selectedPointCount() const { return static_cast<int>(pointSels_.size()); }
    bool discardPointEdits();

    // --- Édition de nœuds (outil « node.edit », un seul objet sélectionné) ------------------
    [[nodiscard]] int nodeHandleCount() const;
    [[nodiscard]] int selectedNodeCount() const;
    // Position de la poignée `index` dans le repère des pixels de l'aperçu (tests, accessibilité).
    [[nodiscard]] QPointF nodeHandlePx(int index) const;
    [[nodiscard]] int tangentHandleCount() const;
    [[nodiscard]] QPointF tangentHandlePx(int index) const;

    // Préréglages de points : enregistrés dans le projet (.osp), appliqués à la sélection.
    bool savePresetNamed(const QString& name); // réglages de l'objet principal de la sélection
    bool applyPresetIndex(int index);
    bool removePresetIndex(int index);

    // --- Affichage, simulation, contexte -------------------------------------------------------
    void setGridSpacing(double mm);
    // Guides posés par l'utilisateur (repères de session, pas dans le document) : positions en mm
    // « modèle » (Y vers le haut). Le déplacement d'objets s'y accroche comme à un bord d'objet.
    void addGuide(bool vertical, double mm);
    void askGuide(bool vertical);
    void clearGuides();
    void setGuideColor(const QColor& color);
    [[nodiscard]] int guideCount() const {
        return static_cast<int>(guidesX_.size() + guidesY_.size());
    }
    // Zoom (menu Affichage › Zoom, palette, clavier).
    void zoomIn();
    void zoomOut();
    void zoomToSelection();
    void zoomActual();
    void zoomBack(); // 0 = pas de grille
    [[nodiscard]] double gridSpacing() const { return gridSpacingMm_; }
    void setThemeMode(const QString& mode); // "system" | "light" | "dark"
    // Qualité du rendu réaliste de l'aperçu (résolution, fil, tissu) : bornée, enregistrée dans les
    // préférences, aperçu recalculé en tâche de fond (regroupé).
    void setRenderSettings(const RenderSettings& settings);
    [[nodiscard]] const RenderSettings& renderSettings() const { return render_; }
    void showRenderQuality();
    void setDensity(UiDensity density); // marges de l'interface (préférence enregistrée)
    [[nodiscard]] UiDensity density() const { return density_; }
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
    // Parcours guidé : écran d'accueil sans projet, bandeau d'étapes avec projet.
    [[nodiscard]] WelcomePage* welcomePage() const { return welcome_; }
    [[nodiscard]] bool welcomeVisible() const;
    [[nodiscard]] QString guideHeadline() const;
    [[nodiscard]] int guideCurrentStep() const { return guide_.currentIndex; }
    [[nodiscard]] const Guide& guide() const { return guide_; }
    // Remplace la boîte « écraser des retouches ? » (tests) ; sans crochet : boîte de dialogue.
    void setConfirmHook(std::function<bool(const QString&)> hook) {
        confirmHook_ = std::move(hook);
    }
    void setCheckSkipped(bool skipped);
    [[nodiscard]] bool checkSkipped() const { return checkSkipped_; }
    [[nodiscard]] NextAction guideNext() const { return guide_.next; }
    bool runGuideNext();
    bool runGuideAction(NextAction action); // vrai si une action a été lancée
    [[nodiscard]] ThreadPanel* threadPanel() const { return threads_; }

    // --- Compagnon IA : pont « projet vivant » (un agent MCP modifie CE document) -------------
    // Désactivé au démarrage ; activé par l'utilisateur seul. Chaque modification d'agent exige
    // son approbation et forme UNE étape de l'historique d'annulation de l'utilisateur.
    bool setLiveBridgeEnabled(bool on, QString* error = nullptr);
    [[nodiscard]] bool liveBridgeEnabled() const;
    [[nodiscard]] QString liveEndpoint() const;
    [[nodiscard]] std::string liveToken() const;
    [[nodiscard]] CompanionPanel* companionPanel() const { return companionPanel_; }
    // Discussion avec l'agent : ouvre le panneau et branche le fournisseur (réel par défaut).
    void openChat();
    // Remplace le fournisseur (tests) ; nullptr revient au fournisseur réel au prochain openChat().
    void setChatBackend(CompanionBackend* backend);
    [[nodiscard]] ChatPanel* chatPanel() const { return chatPanel_; }
    void showCompanionSettings();
    // Le pont d'agent existe (sans serveur local) : requis par le chat, créé à la demande.
    LiveCompanion& ensureCompanion();

    // --- Optimisation de l'ordre de couture, avec mesures avant / après ---------------------
    [[nodiscard]] OptimizePanel* optimizePanel() const { return optimizePanel_; }
    // Correction de la segmentation après numérisation (outil « Régions », X).
    [[nodiscard]] RegionPanel* regionPanel() const { return regionPanel_; }
    [[nodiscard]] RegionEditController* regionController() const { return regions_.get(); }

    // --- Risque de plissement et déformation prévue du tissu (estimation non calibrée) -------
    [[nodiscard]] bool fabricRiskActive() const;
    [[nodiscard]] bool fabricRiskShown() const;
    [[nodiscard]] const std::optional<FabricRisk>& fabricRisk() const;
    void runOptimization(PathStrategy strategy, bool improve);
    bool acceptOptimization();
    void refuseOptimization();
    [[nodiscard]] LiveCompanion* companion() const { return companion_.get(); }
    // Dossier du fichier de découverte (défaut : dossier utilisateur de `libs/mcp`) et réponse
    // d'approbation imposée (tests headless) : sans elle, une boîte de dialogue est affichée.
    void setLiveDirectory(const std::filesystem::path& dir) { liveDir_ = dir; }
    void setApprovalOverride(std::function<bool(const mcp::ApprovalRequest&)> fn) {
        approvalOverride_ = std::move(fn);
    }
    // Carte de densité (surimpression) : affichée, et zones sur-denses de la dernière analyse.
    [[nodiscard]] bool densityShown() const;
    [[nodiscard]] bool densityActive() const;
    [[nodiscard]] const std::vector<DensityHotspotInfo>& densityHotspots() const;
    // Repère mm <-> pixels et index de sélection de l'aperçu affiché (tests, diagnostics).
    [[nodiscard]] const PreviewImage& previewFrame() const { return frame_; }
    // Un point (pixels de l'image d'aperçu) qui appartient à l'objet : pour piloter le canevas en
    // test.
    [[nodiscard]] std::optional<QPointF> objectCentrePx(std::uint64_t id) const;

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override; // Échap : désélectionne les régions

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
    void applyWorkspaceDocks(Workspace workspace);
    void rebuildObjectList();
    void syncTreeFromSelection();
    void onSelectionChangedInTree();
    void onDocumentChanged(ChangeKind kind);
    void requestPreview();
    void setNotice(const QString& text, bool error);
    void refreshActionStates();
    void refreshTaskStatus();
    void chooseAndOpen();
    void chooseAndImportMachine();
    void resegmentImage();
    void chooseAndImportDxf();
    void chooseAndExportDxf();
    void updateSelectionBoxes();
    void onCanvasRect(QRectF scenePx, Qt::KeyboardModifiers modifiers);
    void applyToolToCanvas();
    void updateSourceOverlay();
    void polishDockTabs();
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
    QMenu* allToolsMenu_{nullptr};
    QAction* compactToolsAction_{nullptr};
    bool compactTools_{false};
    QActionGroup* workspaceGroup_{nullptr};
    QActionGroup* toolGroup_{nullptr};
    std::vector<QAction*> workspaceActions_;
    QDoubleSpinBox* heightSpin_{nullptr};
    QTreeWidget* objectTree_{nullptr};
    QLabel* summary_{nullptr};
    QLabel* notice_{nullptr};
    FeedbackCue* feedbackCue_{nullptr};
    QAction* reducedMotionAction_{nullptr};
    RenderSettings render_;
    class RenderQualityDialog* renderDialog_{nullptr};
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
    std::vector<double> guidesX_;
    std::vector<double> guidesY_;
    QColor guideColor_{0, 170, 120};
    void updateGuides();
    void refreshThemedIcons();
    std::vector<int> pointSels_; // poignées choisies (la principale, `pointSel_`, en fait partie)
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
    UiDensity density_{UiDensity::Normal};
    void savePreferencesNow();
    DiagnosticsPanel* diagnostics_{nullptr};
    CalquePanel* calques_{nullptr};
    WelcomePage* welcome_{nullptr};
    QStackedWidget* stack_{nullptr};
    QWidget* guideStrip_{nullptr};
    QLabel* guideSteps_{nullptr};
    QLabel* guideHeadline_{nullptr};
    QPushButton* guideNext_{nullptr};
    Guide guide_;
    std::optional<std::uint64_t> analysedRevision_;
    std::optional<std::uint64_t> exportedRevision_;
    std::filesystem::path lastExportPath_;
    std::optional<std::uint64_t> machineCheckRevision_; // contrôle machine fait pour cette révision
    int analysisWarnings_{0};
    int machineBlockers_{0};
    int machineWarnings_{0};
    bool checkSkipped_{false}; // contrôle facultatif ignoré à la demande de l'utilisateur
    std::function<bool(const QString&)> confirmHook_;
    int analysisErrors_{0};
    void buildCentral();
    void updateGuide();
    ThreadPanel* threads_{nullptr};
    CompanionPanel* companionPanel_{nullptr};
    ChatPanel* chatPanel_{nullptr};
    QDockWidget* chatDock_{nullptr};
    CompanionBackend* chatBackend_{nullptr};         // injecté (tests) ou le fournisseur réel
    ClaudeCompanionBackend* claudeBackend_{nullptr}; // créé à la demande, possédé par Qt (this)
    std::unique_ptr<agent::ISecretStore> secretStore_;
    agent::DiagnosticLog companionLog_;
    OptimizePanel* optimizePanel_{nullptr};
    QPointer<ImportDialog> importDialog_;
    std::unique_ptr<NodeEditController> nodes_;
    std::unique_ptr<RegionEditController> regions_;
    RegionPanel* regionPanel_{nullptr};
    project_ops::DigitizeSettings lastDigitizeSettings_; // réglages de la dernière numérisation
    std::unique_ptr<AnalysisOverlays> overlays_;
    std::uint64_t importPreviewTask_{0};
    bool snapEnabled_{true};
    QPointF snapDelta(QPointF deltaPx);
    std::optional<PathProposal> proposal_;
    std::uint64_t optimizeTask_{0};
    std::unique_ptr<LiveCompanion> companion_;
    std::unique_ptr<LiveServer> liveServer_;
    std::filesystem::path liveDir_;
    std::function<bool(const mcp::ApprovalRequest&)> approvalOverride_;
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
