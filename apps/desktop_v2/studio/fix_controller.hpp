// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio -- CX09 : « Corriger » et « Réduire les fils ». Adaptateur SANS logique métier au-dessus
// de `fix_service` (core, sans Qt) : proposition -> aperçu -> approbation -> UNE étape annulable.
// Le calcul tourne sur un instantané hors du fil d'interface et porte la révision analysée : une
// proposition dont le document a changé depuis est refusée (et signalée périmée à l'écran).

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <optional>

#include "fix_service.hpp"
#include "studio_controller.hpp"
#include "task_manager.hpp"

namespace openstitch::desktop_v2::studio {

class FixController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY messageChanged)
    // Corrections : [{index, label, message, preview, safe}]
    Q_PROPERTY(QVariantList proposals READ proposals NOTIFY plansChanged)
    Q_PROPERTY(int safeCount READ safeCount NOTIFY plansChanged)
    Q_PROPERTY(bool planReady READ planReady NOTIFY plansChanged)
    Q_PROPERTY(bool planStale READ planStale NOTIFY plansChanged)
    // Réduction des fils
    Q_PROPERTY(bool reductionReady READ reductionReady NOTIFY plansChanged)
    Q_PROPERTY(bool reductionStale READ reductionStale NOTIFY plansChanged)
    Q_PROPERTY(int threadsBefore READ threadsBefore NOTIFY plansChanged)
    Q_PROPERTY(int threadsAfter READ threadsAfter NOTIFY plansChanged)
    Q_PROPERTY(int objectsRecoloured READ objectsRecoloured NOTIFY plansChanged)
public:
    FixController(StudioController& studio, QObject* parent = nullptr);
    ~FixController() override;

    [[nodiscard]] bool busy() const { return running_ > 0; }
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }

    // --- « Corriger » --------------------------------------------------------------------------
    Q_INVOKABLE void analyze();
    // Applique la correction `index` / toutes les corrections sûres : une étape d'annulation.
    // Faux (document inchangé) si la proposition est périmée ou plus applicable.
    Q_INVOKABLE bool applyFix(int index);
    Q_INVOKABLE bool applyAllSafe();
    Q_INVOKABLE void discardPlan();
    [[nodiscard]] QVariantList proposals() const;
    [[nodiscard]] int safeCount() const { return plan_ ? static_cast<int>(plan_->safeCount) : 0; }
    [[nodiscard]] bool planReady() const { return plan_.has_value(); }
    [[nodiscard]] bool planStale() const;

    // --- Réduire les fils ----------------------------------------------------------------------
    Q_INVOKABLE void previewReduction(int maxThreads);
    Q_INVOKABLE bool applyReduction();
    Q_INVOKABLE void discardReduction();
    [[nodiscard]] bool reductionReady() const { return reduction_.has_value(); }
    [[nodiscard]] bool reductionStale() const;
    [[nodiscard]] int threadsBefore() const;
    [[nodiscard]] int threadsAfter() const;
    [[nodiscard]] int objectsRecoloured() const;

    Q_INVOKABLE void cancel();
signals:
    void busyChanged();
    void messageChanged();
    void plansChanged();

private:
    void say(bool ok, const QString& text);
    void begin();
    void end();
    bool applyIndices(const std::vector<std::size_t>& indices, const QString& label);

    StudioController& studio_;
    QString message_;
    bool ok_{true};
    int running_{0};
    std::optional<FixPlan> plan_;
    std::optional<ThreadReductionPreview> reduction_;
    QTimer pump_;
    TaskManager tasks_;
};

} // namespace openstitch::desktop_v2::studio
