// SPDX-License-Identifier: Apache-2.0
#include "export_controller.hpp"

#include <QFileInfo>
#include <cctype>
#include <cmath>

#include "openstitch/project_ops/applique_cut.hpp"

namespace openstitch::desktop_v2::studio {

ExportController::ExportController(StudioController& studio, FileController& files, QObject* parent)
    : QObject(parent), studio_(studio), files_(files) {
    files_.setExportDelegate(this);
    pump_.setInterval(10);
    connect(&pump_, &QTimer::timeout, this, [this] { tasks_.pump(); });
    pump_.start();
}
ExportController::~ExportController() {
    files_.setExportDelegate(nullptr);
    tasks_.cancelAll();
    tasks_.waitIdle();
}
void ExportController::say(bool ok, const QString& message) {
    ok_ = ok;
    message_ = message;
    emit changed();
}
QVariantMap ExportController::options() const {
    auto values = files_.exportOptions();
    values.insert(QStringLiteral("format"), QStringLiteral("dst"));
    values.insert(QStringLiteral("compensate"), false);
    values.insert(QStringLiteral("strength"), 1.0);
    values.insert(QStringLiteral("reduceTrims"), false);
    values.insert(QStringLiteral("betweenObjects"), true);
    values.insert(QStringLiteral("withinObject"), true);
    values.insert(QStringLiteral("reorderUnderlay"), true);
    values.insert(QStringLiteral("reorderRegions"), false);
    values.insert(QStringLiteral("sameColorSurface"), false);
    values.insert(QStringLiteral("maxBridgeMm"), 20.0);
    values.insert(QStringLiteral("colorCompanion"), false);
    values.insert(QStringLiteral("companionKind"), QStringLiteral("csv"));
    values.insert(QStringLiteral("trims"), true);
    values.insert(QStringLiteral("colorChanges"), true);
    values.insert(QStringLiteral("stops"), 0);
    return values;
}
void ExportController::exportMachine(const QUrl& url, const QVariantMap& values) {
    if (busy_ || studio_.busy() || files_.busy()) {
        say(false, QStringLiteral("Une opération est déjà en cours."));
        return;
    }
    if (!url.isLocalFile() || url.toLocalFile().isEmpty()) {
        say(false, QStringLiteral("Choisissez un fichier local."));
        return;
    }
    QString local = url.toLocalFile();
    if (QFileInfo(local).suffix().isEmpty()) {
        local += QStringLiteral(".") + values.value(QStringLiteral("format"), "dst").toString();
    }
    const std::filesystem::path path(local.toStdWString());
    AdvancedExportOptions request;
    const auto flag = [&](const char* key, bool fallback) {
        return values.value(QLatin1String(key), fallback).toBool();
    };
    request.routes.reduce_trims = flag("reduceTrims", false);
    auto& routes = request.routes.hidden_travel;
    routes.enabled = request.routes.reduce_trims;
    routes.between_objects = flag("betweenObjects", true);
    routes.within_object = flag("withinObject", true);
    routes.reorder_underlay = flag("reorderUnderlay", true);
    routes.reorder_regions = flag("reorderRegions", false);
    routes.allow_same_color_surface = flag("sameColorSurface", false);
    const double bridge = values.value(QStringLiteral("maxBridgeMm"), 20).toDouble();
    if (!std::isfinite(bridge) || bridge < 0 || bridge > 100) {
        say(false, QStringLiteral("Longueur de pont : entre 0 et 100 mm."));
        return;
    }
    routes.max_bridge_length = to_micrometers(Millimeters{bridge});
    request.colorCompanion = flag("colorCompanion", false);
    const auto companion = values.value(QStringLiteral("companionKind"), "csv").toString();
    if (companion != QLatin1String("csv") && companion != QLatin1String("inf")) {
        say(false, QStringLiteral("Fichier de couleurs : CSV ou INF."));
        return;
    }
    request.companionKind = companion == QLatin1String("inf") ? formats::CompanionKind::Inf
                                                              : formats::CompanionKind::Csv;
    request.registration.compensate = flag("compensate", false);
    request.registration.strength = values.value(QStringLiteral("strength"), 1).toDouble();
    request.machine.trims =
        flag("trims", true) ? formats::TrimMode::Native : formats::TrimMode::Drop;
    request.machine.color_changes = flag("colorChanges", true) ? formats::ColorChangeMode::Emit
                                                               : formats::ColorChangeMode::Drop;
    const int stops = values.value(QStringLiteral("stops"), 0).toInt();
    if (stops < 0 || stops > 2) {
        say(false, QStringLiteral("Codage des arrêts inconnu."));
        return;
    }
    request.machine.stops = static_cast<formats::StopMode>(stops);
    auto name = QFileInfo(local).completeBaseName().toUpper().toLatin1();
    std::string designName;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)))
            designName.push_back(c);
    }
    request.machine.design_name = designName.empty() ? "OPENSTITCH" : designName.substr(0, 16);
    // Les paramètres de génération documentaires sont appliqués par le service historique,
    // en une seule commande ; les options propres au fichier ne modifient pas l'OSP.
    const auto refusal = files_.setExportOptions(values, true);
    if (!refusal.isEmpty()) {
        say(false, refusal);
        return;
    }
    busy_ = true;
    say(true, QStringLiteral("Vérification et export en cours…"));
    const auto snapshot = studio_.session().snapshot();
    tasks_.submit<AdvancedExportReport>(
        "Export machine avancé",
        [snapshot, path, request](const TaskContext& task) {
            return writeAdvancedExport(snapshot, path, request, task);
        },
        [this, local, values](Result<AdvancedExportReport>&& result) {
            busy_ = false;
            if (!result) {
                say(false, QString::fromStdString(result.error().message));
                return;
            }
            lastFile_ = QUrl::fromLocalFile(local);
            lastValues_ = values;
            files_.recordMachineExport(lastFile_);
            say(true,
                QStringLiteral("%1 écrit. %2")
                    .arg(QFileInfo(local).fileName(), QString::fromStdString(result->summary)));
        });
}
void ExportController::exportAgain() {
    if (lastFile_.isEmpty()) {
        say(false, QStringLiteral("Aucun export précédent."));
        return;
    }
    exportMachine(lastFile_, lastValues_);
}
void ExportController::exportAppliqueCut(const QUrl& url) {
    if (busy_ || studio_.busy() || files_.busy())
        return;
    if (!url.isLocalFile()) {
        say(false, QStringLiteral("Choisissez un fichier local SVG ou DXF."));
        return;
    }
    const auto snapshot = studio_.session().snapshot();
    const std::filesystem::path path(url.toLocalFile().toStdWString());
    busy_ = true;
    say(true, QStringLiteral("Export du contour de découpe…"));
    tasks_.submit<project_ops::AppliqueCutReport>(
        "Contour de découpe",
        [snapshot, path](const TaskContext& task) -> Result<project_ops::AppliqueCutReport> {
            if (task.cancelled())
                return cancelled_error();
            return project_ops::export_applique_cut(*snapshot, path);
        },
        [this](Result<project_ops::AppliqueCutReport>&& result) {
            busy_ = false;
            say(result.has_value(),
                result ? QStringLiteral("%1 contours de découpe écrits.").arg(result->paths)
                       : QString::fromStdString(result.error().message));
        });
}
void ExportController::cancel() {
    tasks_.cancelAll();
    busy_ = false;
    say(false, QStringLiteral("Export annulé."));
}

} // namespace openstitch::desktop_v2::studio
