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
#include <optional>
#include <vector>

#include "command_catalog.hpp"
#include "diagnostics_service.hpp"
#include "digitize_pipeline.hpp"
#include "document_session.hpp"
#include "edit_batch.hpp"
#include "preview_service.hpp"
#include "shape_batch.hpp"
#include "task_manager.hpp"
#include "tool_registry.hpp"

class QAction;
class QActionGroup;
class QDockWidget;
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
    // Calcule la séquence EFFECTIVE du document courant puis écrit le DST, en tâche annulable.
    void exportDst(const std::filesystem::path& dst);

    // Commandes (palette Ctrl+K, menus, tests) : un identifiant stable par intention.
    [[nodiscard]] std::vector<CommandEntry> commandEntries() const;
    bool runCommand(const QString& id); // faux si inconnue ou désactivée
    void showCommandPalette();
    void selectAllObjects();
    void newProject(); // projet vierge à cadre par défaut : point de départ du dessin

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
    // Repère mm <-> pixels et index de sélection de l'aperçu affiché (tests, diagnostics).
    [[nodiscard]] const PreviewImage& previewFrame() const { return frame_; }
    // Un point (pixels de l'image d'aperçu) qui appartient à l'objet : pour piloter le canevas en
    // test.
    [[nodiscard]] std::optional<QPointF> objectCentrePx(std::uint64_t id) const;

private:
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
    QActionGroup* workspaceGroup_{nullptr};
    QActionGroup* toolGroup_{nullptr};
    std::vector<QAction*> workspaceActions_;
    QDoubleSpinBox* heightSpin_{nullptr};
    QTreeWidget* objectTree_{nullptr};
    QLabel* summary_{nullptr};
    QLabel* notice_{nullptr};
    QProgressBar* progress_{nullptr};
    QToolButton* cancelButton_{nullptr};
    QAction* undoAction_{nullptr};
    QAction* redoAction_{nullptr};
    QAction* saveAction_{nullptr};
    QAction* exportAction_{nullptr};
    QTimer* pump_{nullptr};
    QTimer* previewDebounce_{nullptr};
    InspectorPanel* inspector_{nullptr};
    DiagnosticsPanel* diagnostics_{nullptr};
    QDockWidget* docDock_{nullptr};
    QDockWidget* diagDock_{nullptr};
    PreviewImage frame_; // repère mm/pixels et index de sélection de l'aperçu affiché

    DocumentSession::Token sessionToken_{0};
    SelectionModel::Token selectionToken_{0};
    bool syncingTree_{false};
    TaskId previewTask_{0};
    std::uint64_t previewRevision_{0};
    bool previewShown_{false};
    bool documentOpen_{false}; // un projet est ouvert (même vide) : l'aperçu montre son cadre
    std::filesystem::path currentPath_;
};

} // namespace openstitch::desktop_v2
