// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio -- espace « Points » : adaptateur Qt SANS logique métier. Il LIT le document (objets de
// broderie et lien `source_vector` vers leur forme) pour le bandeau d'état de l'étape « points »
// et pour la liste des objets (lien vers la forme source), et garde la caméra de l'espace pour la
// durée de la session. Rien n'est écrit dans le document : les points ne sont jamais stockés
// (`effective_sequence`), ils suivent leurs formes à chaque changement.

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class PointsSpaceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int objectCount READ objectCount NOTIFY changed)
    Q_PROPERTY(int orphanCount READ orphanCount NOTIFY changed)
    // "empty" | "upToDate" | "stale" (la même nomenclature que `WorkspaceController`).
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    // id d'objet de broderie -> {vectorId, label, found}.
    Q_PROPERTY(QVariantMap sources READ sources NOTIFY changed)
    Q_PROPERTY(QVariantMap camera READ camera NOTIFY cameraChanged)
public:
    explicit PointsSpaceController(StudioController& studio, QObject* parent = nullptr);

    [[nodiscard]] int objectCount() const { return objectCount_; }
    [[nodiscard]] int orphanCount() const { return orphanCount_; }
    [[nodiscard]] QString state() const { return state_; }
    [[nodiscard]] QString summary() const { return summary_; }
    [[nodiscard]] QVariantMap sources() const { return sources_; }
    [[nodiscard]] QVariantMap camera() const { return camera_; }

    // Caméra propre à l'espace (centre en mm, zoom) : mémorisée au démontage, restituée au
    // montage, effacée par un nouveau document.
    Q_INVOKABLE void rememberCamera(const QVariantMap& view);
    Q_INVOKABLE void clearCamera();
    // Vrai si l'objet de broderie est relié à une forme source qui existe encore.
    Q_INVOKABLE [[nodiscard]] bool hasSource(const QString& objectId) const;
signals:
    void changed();
    void cameraChanged();

private:
    void refresh();

    StudioController& studio_;
    int objectCount_{0};
    int orphanCount_{0};
    QString state_{QStringLiteral("empty")};
    QString summary_;
    QVariantMap sources_;
    QVariantMap camera_;
};

} // namespace openstitch::desktop_v2::studio
