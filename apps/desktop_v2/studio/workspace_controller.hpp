// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio -- adaptateur Qt des QUATRE ESPACES (image, segmentation, vector, points). Aucune
// logique métier : l'état de base vient du calcul pur `computeStageStatus` (core/stage_status),
// chaque espace PUBLIE l'état de SON étape par `setStageStatus` (il l'emporte sur le calcul de
// base tant qu'il n'est pas retiré), et l'hôte (`SpaceHost.qml`) conserve la vue (caméra) d'un
// espace à l'autre. La sélection vit déjà dans `DocumentSession` : elle traverse les espaces sans
// copie. Contrat complet : docs/desktop-v2/23-espaces-studio.md.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <array>
#include <optional>

#include "stage_status.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class WorkspaceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(
        StudioController* controller READ controller WRITE setController NOTIFY controllerChanged)
    // [{id, label, shortcut, state, detail, actionLabel, actionEnabled, index}] dans l'ordre du
    // pipeline.
    Q_PROPERTY(QVariantList spaces READ spaces NOTIFY spacesChanged)
    Q_PROPERTY(QString current READ current WRITE setCurrent NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    // {centerX, centerY, zoom, selectionCount} : dernière vue sauvegardée (vide au départ).
    Q_PROPERTY(QVariantMap view READ view NOTIFY viewChanged)
    // Bandeau : état de l'étape AMONT de l'espace courant {visible, stage, state, text,
    // actionLabel, actionEnabled}.
    Q_PROPERTY(QVariantMap banner READ banner NOTIFY spacesChanged)
    // Corrections de régions non répercutées sur les objets (même notion que le guide de parcours).
    Q_PROPERTY(bool segmentationDirty READ segmentationDirty WRITE setSegmentationDirty NOTIFY
                   spacesChanged)
    Q_PROPERTY(QString dirtyReason READ dirtyReason WRITE setDirtyReason NOTIFY spacesChanged)
    // Préférences de panneaux partagées par les espaces (restaurées avec la session).
    Q_PROPERTY(bool treeMode READ treeMode WRITE setTreeMode NOTIFY panelsChanged)
    Q_PROPERTY(bool editOpen READ editOpen WRITE setEditOpen NOTIFY panelsChanged)
public:
    explicit WorkspaceController(QObject* parent = nullptr);

    [[nodiscard]] StudioController* controller() const { return controller_; }
    void setController(StudioController* controller);
    [[nodiscard]] QVariantList spaces() const;
    [[nodiscard]] QString current() const { return current_; }
    Q_INVOKABLE void setCurrent(const QString& id);
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] QVariantMap view() const;
    [[nodiscard]] QVariantMap banner() const;
    [[nodiscard]] bool segmentationDirty() const { return segmentationDirty_; }
    void setSegmentationDirty(bool dirty);
    [[nodiscard]] QString dirtyReason() const { return dirtyReason_; }
    void setDirtyReason(const QString& reason);
    [[nodiscard]] bool treeMode() const { return treeMode_; }
    void setTreeMode(bool value);
    [[nodiscard]] bool editOpen() const { return editOpen_; }
    void setEditOpen(bool value);

    // L'espace `id` publie l'état de son étape ("empty"|"upToDate"|"stale"|"blocked").
    Q_INVOKABLE void setStageStatus(const QString& id, const QString& state, const QString& detail);
    // Retire la publication : le calcul de base reprend la main.
    Q_INVOKABLE void clearStageStatus(const QString& id);
    // Libellé du bouton « mettre à jour cette étape » du bandeau ; vide = pas de bouton.
    Q_INVOKABLE void setStageAction(const QString& id, const QString& label, bool enabled = true);
    // Le bouton du bandeau : annonce `updateRequested(id)` ; l'espace `id` l'exécute (une seule
    // étape d'annulation, via DocumentSession).
    Q_INVOKABLE void requestUpdate(const QString& id);
    // Sauvegarde la vue du canevas qui quitte la scène (voir GpuCanvas::viewState).
    Q_INVOKABLE void saveView(const QVariantMap& view);
    Q_INVOKABLE void clearView();
    Q_INVOKABLE [[nodiscard]] int indexOf(const QString& id) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap space(const QString& id) const;
signals:
    void controllerChanged();
    void spacesChanged();
    void currentChanged();
    void viewChanged();
    void panelsChanged();
    void updateRequested(const QString& id);

private:
    struct Published {
        std::optional<StageState> state;
        QString detail;
        QString actionLabel;
        bool actionEnabled{true};
    };
    void invalidate();
    [[nodiscard]] const std::array<StageStatus, 4>& base() const;
    [[nodiscard]] StageState stateOf(int index) const;
    [[nodiscard]] QString detailOf(int index) const;

    StudioController* controller_{nullptr};
    QString current_;
    QVariantMap view_;
    bool segmentationDirty_{false};
    QString dirtyReason_;
    bool treeMode_{false};
    bool editOpen_{false};
    std::array<Published, 4> published_;
    mutable std::array<StageStatus, 4> base_;
    mutable bool baseValid_{false};
    bool refreshQueued_{false};
};

// Enregistre WorkspaceController dans OpenStitch.Studio 1.0 (appelé au démarrage de l'application
// Qt par gpu_canvas.cpp : les tests qui chargent Main.qml n'ont rien à faire).
void registerWorkspaceTypes();

} // namespace openstitch::desktop_v2::studio
