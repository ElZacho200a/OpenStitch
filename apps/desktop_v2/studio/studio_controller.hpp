// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <memory>

#include "document_session.hpp"
#include "studio_scene.hpp"
#include "task_manager.hpp"
#include "tool_registry.hpp"

namespace openstitch::desktop_v2::studio {

// Qt adapter over the existing session; it owns neither a second document nor an undo stack.
class StudioController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantList objects READ objects NOTIFY changed)
    Q_PROPERTY(QString selectionInfo READ selectionInfo NOTIFY changed)
    Q_PROPERTY(int commandCount READ commandCount NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY changed)
    Q_PROPERTY(QString redoLabel READ redoLabel NOTIFY changed)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY changed)
    Q_PROPERTY(QString primarySelectionId READ primarySelectionId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectionTool READ selectionTool CONSTANT)
    Q_PROPERTY(QVariantMap panTool READ panTool CONSTANT)
public:
    explicit StudioController(DocumentSession& session, QObject* parent = nullptr);
    ~StudioController() override;
    [[nodiscard]] DocumentSession& session() const { return session_; }
    [[nodiscard]] const std::shared_ptr<const StudioSceneSnapshot>& scene() const { return scene_; }
    [[nodiscard]] QString title() const { return title_; }
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] QVariantList objects() const;
    [[nodiscard]] QString selectionInfo() const;
    [[nodiscard]] int commandCount() const;
    [[nodiscard]] QVariantMap selectionTool() const { return toolInfo("select"); }
    [[nodiscard]] QVariantMap panTool() const { return toolInfo("pan"); }
    [[nodiscard]] bool dirty() const { return session_.dirty(); }
    [[nodiscard]] bool canUndo() const { return session_.canUndo(); }
    [[nodiscard]] bool canRedo() const { return session_.canRedo(); }
    [[nodiscard]] QString undoLabel() const;
    [[nodiscard]] QString redoLabel() const;
    [[nodiscard]] bool hasSelection() const { return !session_.selection().targets().empty(); }
    [[nodiscard]] QString primarySelectionId() const {
        const auto primary = session_.selection().primary();
        return primary && primary->kind == TargetKind::EmbroideryObject
                   ? QString::number(primary->id)
                   : QString{};
    }
    Q_INVOKABLE void newProject();
    // Both return true when the project was written; the status line carries the reason otherwise.
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAs(const QUrl& url);
    Q_INVOKABLE [[nodiscard]] bool hasPath() const { return !path_.isEmpty(); }
    // Famille A (FileController) : chemin du projet courant et installation d'un projet venu d'un
    // import / d'une récupération, avec le même état (titre, chemin, historique vierge) qu'une
    // ouverture. `clean` : le document est considéré comme enregistré.
    [[nodiscard]] QString path() const { return path_; }
    void adoptProject(document::Project project, const QString& path, const QString& title,
                      bool clean);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Moves the selected embroidery objects by a model-space offset in mm (one undo step).
    Q_INVOKABLE bool moveSelection(double dxMm, double dyMm);
    Q_INVOKABLE void openProject(const QUrl& url);
    Q_INVOKABLE void selectObject(const QString& id, bool toggle = false);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void sceneChanged();
    void documentReplaced();
    void selectionChanged();
    void loadFinished(bool success);

private:
    void rebuildScene();
    bool writeProject(const QString& path);
    [[nodiscard]] QVariantMap toolInfo(std::string_view id) const;
    DocumentSession& session_;
    const ToolRegistry tools_{ToolRegistry::builtin()};
    DocumentSession::Token documentToken_{};
    SelectionModel::Token selectionToken_{};
    std::shared_ptr<const StudioSceneSnapshot> scene_;
    QString title_{QStringLiteral("Sans projet")};
    QString status_{QStringLiteral("Ouvrez un projet .osp pour explorer sa broderie")};
    QString path_;
    bool busy_{false};
    std::uint64_t request_{0};
    std::uint64_t lastNotifiedRevision_{0};
    QTimer pump_;
    // Destroyed before the session, the timer and this adapter's other data.
    TaskManager tasks_;
};

} // namespace openstitch::desktop_v2::studio
