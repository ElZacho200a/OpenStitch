// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <memory>
#include <optional>

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
    // Liste des objets : notifiée seulement quand le document change (pas pour un clic de
    // sélection, qui recréerait toutes les lignes QML). L'état sélectionné : selectedIdSet.
    Q_PROPERTY(QVariantList objects READ objects NOTIFY objectsChanged)
    Q_PROPERTY(QVariantMap selectedIdSet READ selectedIdSet NOTIFY selectionChanged)
    Q_PROPERTY(int selectedObjectCount READ selectedObjectCount NOTIFY selectionChanged)
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
    // Satin de la génération automatique des objets, UN seul état pour toute l'interface
    // (onglet Segmentation, « Régénérer les objets », « Convertir en broderie » des Vecteurs).
    // Clés de `objectDigitizeSettingsFrom` (segmentation_settings.hpp) ; absente = défaut moteur.
    Q_PROPERTY(
        QVariantMap satinOptions READ satinOptions WRITE setSatinOptions NOTIFY satinOptionsChanged)
public:
    explicit StudioController(DocumentSession& session, QObject* parent = nullptr);
    ~StudioController() override;
    [[nodiscard]] DocumentSession& session() const { return session_; }
    [[nodiscard]] const std::shared_ptr<const StudioSceneSnapshot>& scene() const { return scene_; }
    // Vrai pendant qu'une reconstruction est en cours : la scène affichée date d'avant la dernière
    // édition (elle est conservée plutôt qu'effacée).
    [[nodiscard]] bool sceneStale() const {
        return scene_ && scene_->revision != session_.revision();
    }
    [[nodiscard]] QString title() const { return title_; }
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] bool busy() const { return busy_; }
    // Nombre de reconstructions complètes de la scène lancées depuis la création (PERF-005) :
    // permet de prouver qu'une modification sans effet sur la broderie n'en déclenche aucune.
    [[nodiscard]] int sceneBuildCount() const { return sceneBuilds_; }
    [[nodiscard]] QVariantList objects() const;
    // Identifiants (texte) des objets de broderie sélectionnés -> true ; recherche O(1) en QML.
    [[nodiscard]] QVariantMap selectedIdSet() const;
    [[nodiscard]] int selectedObjectCount() const;
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
    [[nodiscard]] QVariantMap satinOptions() const { return satinOptions_; }
    void setSatinOptions(const QVariantMap& options) {
        if (options != satinOptions_) {
            satinOptions_ = options;
            emit satinOptionsChanged();
        }
    }
    Q_INVOKABLE void newProject();
    // Variante C++ : part d'un projet neuf déjà préparé (p. ex. le dernier cadre choisi).
    void newProject(document::Project seed);
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
    void objectsChanged();
    void loadFinished(bool success);
    void satinOptionsChanged();

private:
    [[nodiscard]] QVariantList buildObjects() const;
    mutable std::optional<QVariantList> objectsCache_;
    std::optional<std::uint64_t> objectsStamp_; // contentStamp() de la liste diffusée
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
    QVariantMap satinOptions_;
    bool busy_{false};
    int sceneBuilds_{0};
    std::uint64_t request_{0};
    std::uint64_t lastNotifiedRevision_{0};
    // Destroyed before the session, the timer and this adapter's other data.
    TaskManager tasks_;
};

} // namespace openstitch::desktop_v2::studio
