// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio -- famille I : compensation de tirage. Adaptateur Qt Quick SANS logique métier : mode de
// compensation de l'objet (via l'inspecteur, donc `param_spec` + une commande annulable),
// étalonnage sur pièce-test (`PullCalibration`), comparaison nominal / prédit
// (`compareProjectPull`) et décalage entre couleurs (`project_ops::predict_color_registration`,
// hors fil d'interface par TaskManager). HONNÊTETÉ : aucun chiffre n'est mesuré sur machine ; le
// modèle n'est pas validé physiquement.

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <optional>

#include "inspector_controller.hpp"
#include "pull_calibration.hpp"
#include "studio_controller.hpp"
#include "task_manager.hpp"

namespace openstitch::desktop_v2::studio {

class PullController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList modes READ modes CONSTANT)
    Q_PROPERTY(int mode READ mode NOTIFY changed) // -1 : aucun objet compatible
    Q_PROPERTY(QString fabric READ fabric NOTIFY changed)
    Q_PROPERTY(QStringList fabrics READ fabrics CONSTANT)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(int measuredCount READ measuredCount NOTIFY changed)
    Q_PROPERTY(double rmsMm READ rmsMm NOTIFY changed)
    Q_PROPERTY(QVariantMap fit READ fit NOTIFY changed)
    Q_PROPERTY(QVariantMap comparison READ comparison NOTIFY changed)
    Q_PROPERTY(QVariantMap registration READ registration NOTIFY changed)
    // Le décalage prédit décrit UNE révision : après toute modification il est périmé et le DST
    // compensé n'est plus exportable.
    Q_PROPERTY(bool registrationStale READ registrationStale NOTIFY changed)
    Q_PROPERTY(bool canExportCompensated READ canExportCompensated NOTIFY changed)
    Q_PROPERTY(QString limits READ limits CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY messageChanged)
public:
    PullController(StudioController& studio, InspectorController* inspector,
                   QObject* parent = nullptr);
    ~PullController() override;

    [[nodiscard]] QStringList modes() const;
    [[nodiscard]] int mode() const;
    [[nodiscard]] QString fabric() const;
    [[nodiscard]] QStringList fabrics() const;
    [[nodiscard]] QVariantList rows() const;
    [[nodiscard]] int measuredCount() const {
        return static_cast<int>(calibration_.measuredCount());
    }
    [[nodiscard]] double rmsMm() const { return calibration_.rmsMm(); }
    [[nodiscard]] QVariantMap fit() const;
    [[nodiscard]] QVariantMap comparison() const { return comparison_; }
    [[nodiscard]] QVariantMap registration() const { return registration_; }
    [[nodiscard]] bool registrationStale() const;
    [[nodiscard]] bool canExportCompensated() const;
    [[nodiscard]] QString limits() const;
    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }

    // 0 manuel, 1 automatique, 2 adaptatif, 3 prédictif : sur la sélection, une étape d'annulation.
    Q_INVOKABLE bool setMode(int mode);
    Q_INVOKABLE bool setFabric(const QString& name);
    // Pièce-test : mesures saisies, effacées, importées / exportées (CSV colonne `measured_mm`).
    Q_INVOKABLE bool setMeasure(const QString& id, double millimeters);
    Q_INVOKABLE bool clearMeasure(const QString& id);
    Q_INVOKABLE void clearMeasures();
    Q_INVOKABLE bool importCalibration(const QString& path);
    Q_INVOKABLE bool exportCalibration(const QString& path);
    Q_INVOKABLE bool writeTestPiece(const QString& path);
    // Ce que le tirage prédit sur le motif ouvert (lecture seule, séquence effective).
    Q_INVOKABLE bool compare();
    // Décalage entre couleurs prédit (hors fil d'interface) ; `strength` : contre-compensation
    // 0..1.
    Q_INVOKABLE void predictRegistration(double strength);
    // Écrit le DST de la séquence compensée (jamais stockée dans le document).
    Q_INVOKABLE bool exportCompensatedDst(const QString& path);
signals:
    void changed();
    void messageChanged();

private:
    std::uint64_t gateStamp_{0}; // documentStamp() au dernier changed rediffusé
    void say(bool ok, const QString& text);

    StudioController& studio_;
    InspectorController* inspector_;
    PullCalibration calibration_;
    TaskManager tasks_;
    QVariantMap comparison_;
    QVariantMap registration_;
    std::vector<std::uint8_t> compensatedDst_;
    std::optional<std::uint64_t> registrationRevision_;
    bool busy_{false};
    QString message_;
    bool ok_{true};
};

} // namespace openstitch::desktop_v2::studio
