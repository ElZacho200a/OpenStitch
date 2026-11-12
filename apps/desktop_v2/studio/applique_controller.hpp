// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio -- famille I : appliqué. Adaptateur SANS logique métier : conversion et réglages (point de
// placement, maintien, couverture, décalage de découpe, arrêts) passent par l'inspecteur
// (`param_spec` + une commande annulable) ; l'export du contour de découpe par
// `project_ops::export_applique_cut` (lecture seule du document).

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <cstdint>

#include "inspector_controller.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class AppliqueController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isApplique READ isApplique NOTIFY changed)
    Q_PROPERTY(bool canConvert READ canConvert NOTIFY changed)
    Q_PROPERTY(int appliqueCount READ appliqueCount NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY messageChanged)
public:
    AppliqueController(StudioController& studio, InspectorController& inspector,
                       QObject* parent = nullptr);

    [[nodiscard]] bool isApplique() const;
    [[nodiscard]] bool canConvert() const;
    [[nodiscard]] int appliqueCount() const;
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }

    // Convertit la sélection en appliqué : une étape d'annulation.
    Q_INVOKABLE bool convert();
    // Réglages du placement, du maintien, de la couverture, de la découpe : {key, value, min, max}.
    Q_INVOKABLE [[nodiscard]] QVariantMap field(const QString& key) const;
    Q_INVOKABLE bool setField(const QString& key, double value);
    // Écrit le contour de découpe (.svg ou .dxf) de l'appliqué sélectionné, ou de tous si aucun
    // n'est sélectionné.
    Q_INVOKABLE bool exportCut(const QString& path);
signals:
    void changed();
    void messageChanged();

private:
    std::uint64_t gateStamp_{0}; // documentStamp() au dernier changed rediffusé
    void say(bool ok, const QString& text);
    [[nodiscard]] std::uint64_t selectedApplique() const;

    StudioController& studio_;
    InspectorController& inspector_;
    QString message_;
    bool ok_{true};
};

} // namespace openstitch::desktop_v2::studio
