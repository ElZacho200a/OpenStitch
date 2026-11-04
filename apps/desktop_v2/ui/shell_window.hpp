// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- fenêtre principale (« Application Shell »). Elle CÂBLE et AFFICHE ; elle ne calcule
// rien : le document vit dans DocumentSession, la sélection dans SelectionModel, les calculs longs
// dans TaskManager, les outils dans ToolRegistry. Les widgets s'abonnent à ces modèles au lieu de
// se synchroniser entre eux (principe I : une sélection, une source de vérité).

#include <QMainWindow>
#include <QPointF>
#include <filesystem>
#include <optional>
#include <vector>

#include "digitize_pipeline.hpp"
#include "document_session.hpp"
#include "preview_service.hpp"
#include "task_manager.hpp"
#include "tool_registry.hpp"

class QAction;
class QActionGroup;
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
    PreviewImage frame_; // repère mm/pixels et index de sélection de l'aperçu affiché

    DocumentSession::Token sessionToken_{0};
    SelectionModel::Token selectionToken_{0};
    bool syncingTree_{false};
    TaskId previewTask_{0};
    std::uint64_t previewRevision_{0};
    bool previewShown_{false};
    std::filesystem::path currentPath_;
};

} // namespace openstitch::desktop_v2
