// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "density_service.hpp"
#include "diagnostics_service.hpp"
#include "fabric_risk.hpp"
#include "hoop_store.hpp"
#include "path_optimizer.hpp"
#include "preview_service.hpp"
#include "render_settings.hpp"
#include "task_manager.hpp"
#include "view_controller.hpp"

namespace openstitch::desktop_v2::studio {

// Qt adapter over the analysis services. Long computations run on a TaskManager worker over an
// immutable snapshot and carry the document revision they analysed: a result for an older
// revision is flagged stale (maps are withdrawn from the canvas, never shown against a changed
// document), and an order proposal for an older revision is refused by applyProposal. Mutations
// (order, hoop, machine) are single ICommands through DocumentSession.
class AnalysisController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY messageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyLabel READ busyLabel NOTIFY busyChanged)
    // Diagnostics: [{index, severity, code, message, suggestion, objects, hasPosition, xMm, yMm}]
    Q_PROPERTY(QVariantList findings READ findings NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool diagnosticsReady READ diagnosticsReady NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool diagnosticsStale READ diagnosticsStale NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString diagnosticsSummary READ diagnosticsSummary NOTIFY diagnosticsChanged)
    // Density map
    Q_PROPERTY(bool densityReady READ densityReady NOTIFY densityChanged)
    Q_PROPERTY(QString densitySummary READ densitySummary NOTIFY densityChanged)
    Q_PROPERTY(QVariantList densityHotspots READ densityHotspots NOTIFY densityChanged)
    // Fabric pull / wrinkle risk
    Q_PROPERTY(bool riskReady READ riskReady NOTIFY riskChanged)
    Q_PROPERTY(QString riskVerdict READ riskVerdict NOTIFY riskChanged)
    Q_PROPERTY(QString riskLevel READ riskLevel NOTIFY riskChanged)
    // Order optimisation with before/after proof
    Q_PROPERTY(QStringList strategies READ strategies CONSTANT)
    Q_PROPERTY(bool proposalReady READ proposalReady NOTIFY proposalChanged)
    Q_PROPERTY(bool proposalStale READ proposalStale NOTIFY proposalChanged)
    Q_PROPERTY(bool proposalImproves READ proposalImproves NOTIFY proposalChanged)
    Q_PROPERTY(QStringList proposalSummary READ proposalSummary NOTIFY proposalChanged)
    // Hoops and machine
    Q_PROPERTY(QVariantList libraryHoops READ libraryHoops CONSTANT)
    Q_PROPERTY(QVariantList userHoops READ userHoops NOTIFY hoopsChanged)
    Q_PROPERTY(QVariantMap currentHoop READ currentHoop NOTIFY hoopsChanged)
    Q_PROPERTY(QVariantList machines READ machines CONSTANT)
    Q_PROPERTY(QString machineId READ machineId NOTIFY hoopsChanged)
    // Realistic render settings and preview
    Q_PROPERTY(QVariantMap renderSettings READ renderSettings NOTIFY renderChanged)
    Q_PROPERTY(QVariantList renderPresets READ renderPresets CONSTANT)
    Q_PROPERTY(bool realisticReady READ realisticReady NOTIFY renderChanged)
public:
    // `configDir` keeps the user hoops and the render preferences; empty = in memory only.
    AnalysisController(StudioController& studio, ViewController& view,
                       std::filesystem::path configDir = {}, QObject* parent = nullptr);
    ~AnalysisController() override;

    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }
    [[nodiscard]] bool busy() const { return running_ > 0; }
    [[nodiscard]] QString busyLabel() const { return busyLabel_; }

    // --- Diagnostics ---------------------------------------------------------------------------
    Q_INVOKABLE void runDiagnostics();
    // Centres the view on the finding (marker at its position, else frames its objects).
    Q_INVOKABLE bool centerOnFinding(int index);
    Q_INVOKABLE bool selectFindingObjects(int index);
    [[nodiscard]] QVariantList findings() const;
    [[nodiscard]] bool diagnosticsReady() const { return diagnostics_.has_value(); }
    [[nodiscard]] bool diagnosticsStale() const;
    [[nodiscard]] QString diagnosticsSummary() const;

    // --- Maps ----------------------------------------------------------------------------------
    Q_INVOKABLE void computeDensity();
    Q_INVOKABLE void clearDensity();
    Q_INVOKABLE void computeFabricRisk();
    Q_INVOKABLE void clearFabricRisk();
    [[nodiscard]] bool densityReady() const { return density_.has_value(); }
    [[nodiscard]] QString densitySummary() const;
    [[nodiscard]] QVariantList densityHotspots() const;
    [[nodiscard]] bool riskReady() const { return risk_.has_value(); }
    [[nodiscard]] QString riskVerdict() const;
    [[nodiscard]] QString riskLevel() const;

    // --- Order optimisation --------------------------------------------------------------------
    Q_INVOKABLE void proposeOrder(int strategy, bool improve);
    Q_INVOKABLE bool applyProposal();
    Q_INVOKABLE void discardProposal();
    [[nodiscard]] QStringList strategies() const;
    [[nodiscard]] bool proposalReady() const { return proposal_.has_value(); }
    [[nodiscard]] bool proposalStale() const;
    [[nodiscard]] bool proposalImproves() const { return proposal_ && proposal_->improves(); }
    [[nodiscard]] QStringList proposalSummary() const;

    // --- Hoops and machine ---------------------------------------------------------------------
    Q_INVOKABLE bool applyLibraryHoop(const QString& id, double marginMm = 0.0);
    // shape: "rectangle" | "ellipse" | "rounded"; refusals come from validateUserHoop.
    Q_INVOKABLE bool saveUserHoop(const QString& name, double widthMm, double heightMm,
                                  const QString& shape, double cornerRadiusMm, double marginMm);
    Q_INVOKABLE bool saveCurrentHoopAs(const QString& name);
    Q_INVOKABLE bool applyUserHoop(const QString& name);
    Q_INVOKABLE bool removeUserHoop(const QString& name);
    // Do all objects fit the hoop (minus margin) and the machine field? Markers flag the others.
    Q_INVOKABLE QVariantMap checkHoop();
    Q_INVOKABLE bool setMachine(const QString& id);
    [[nodiscard]] QVariantList libraryHoops() const;
    [[nodiscard]] QVariantList userHoops() const;
    [[nodiscard]] QVariantMap currentHoop() const;
    [[nodiscard]] QVariantList machines() const;
    [[nodiscard]] QString machineId() const;

    // --- Render settings -----------------------------------------------------------------------
    Q_INVOKABLE bool setRenderSetting(const QString& key, const QString& value);
    Q_INVOKABLE bool applyRenderPreset(const QString& id);
    Q_INVOKABLE void computeRealistic();
    Q_INVOKABLE void clearRealistic();
    [[nodiscard]] QVariantMap renderSettings() const;
    [[nodiscard]] QVariantList renderPresets() const;
    [[nodiscard]] bool realisticReady() const { return realisticRevision_.has_value(); }

    Q_INVOKABLE void cancel();
signals:
    void messageChanged();
    void busyChanged();
    void diagnosticsChanged();
    void densityChanged();
    void riskChanged();
    void proposalChanged();
    void hoopsChanged();
    void renderChanged();

private:
    void say(bool ok, const QString& text);
    void begin(const QString& label);
    void end();
    void onDocumentChanged();
    void saveHoops();
    void savePreferencesFile();
    StudioController& studio_;
    ViewController& view_;
    std::filesystem::path hoopFile_;
    std::filesystem::path prefsFile_;
    QString message_;
    bool ok_{true};
    int running_{0};
    QString busyLabel_;
    std::uint64_t lastRevision_{0};
    std::optional<DiagnosticsResult> diagnostics_;
    std::optional<DensityOverlay> density_;
    std::optional<FabricRisk> risk_;
    std::optional<PathProposal> proposal_;
    std::optional<std::uint64_t> realisticRevision_;
    std::vector<UserHoop> userHoops_;
    RenderSettings render_;
    QTimer pump_;
    TaskManager tasks_;
};

} // namespace openstitch::desktop_v2::studio
