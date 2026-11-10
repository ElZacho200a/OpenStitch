// SPDX-License-Identifier: Apache-2.0
#include "pull_controller.hpp"

#include "change_gate.hpp"
#include "task_pump.hpp"

#include <QFile>
#include <QSaveFile>
#include <algorithm>

#include "openstitch/project_ops/color_registration.hpp"

namespace openstitch::desktop_v2::studio {
namespace {
QString q(const std::string& s) {
    return QString::fromStdString(s);
}
} // namespace

PullController::PullController(StudioController& studio, InspectorController* inspector,
                               QObject* parent)
    : QObject(parent), studio_(studio), inspector_(inspector) {
    attachTaskPump(tasks_, this);
    gateStamp_ = documentStamp(studio_);
    connect(&studio_, &StudioController::changed, this, [this] {
        const auto stamp = documentStamp(studio_);
        if (stamp != gateStamp_) {
            gateStamp_ = stamp;
            emit changed();
        }
    });
    if (inspector_ != nullptr) {
        connect(inspector_, &QObject::destroyed, this, [this] { inspector_ = nullptr; });
    }
}

PullController::~PullController() {
    tasks_.cancelAll();
    tasks_.waitIdle();
}

QStringList PullController::modes() const {
    return {QStringLiteral("Manuel"), QStringLiteral("Automatique"), QStringLiteral("Adaptatif"),
            QStringLiteral("Prédictif")};
}

int PullController::mode() const {
    if (inspector_ == nullptr) {
        return -1;
    }
    const auto field = inspector_->field(QStringLiteral("pull_mode"));
    if (field.isEmpty() || field.value(QStringLiteral("mixed")).toBool()) {
        return -1;
    }
    return static_cast<int>(field.value(QStringLiteral("value")).toDouble());
}

QString PullController::fabric() const {
    return q(calibration_.fabric());
}

QStringList PullController::fabrics() const {
    QStringList list;
    for (const auto& name : PullCalibration::fabricNames()) {
        list << q(name);
    }
    return list;
}

QVariantList PullController::rows() const {
    QVariantList list;
    for (const auto& r : calibration_.rows()) {
        list.push_back(QVariantMap{{QStringLiteral("id"), q(r.id)},
                                   {QStringLiteral("description"), q(r.description)},
                                   {QStringLiteral("variable"), q(r.variable)},
                                   {QStringLiteral("nominalMm"), r.nominalMm},
                                   {QStringLiteral("predictedMm"), r.predictedMm},
                                   {QStringLiteral("hasMeasure"), r.hasMeasure},
                                   {QStringLiteral("measuredMm"), r.measuredMm},
                                   {QStringLiteral("errorMm"), r.errorMm}});
    }
    return list;
}

QVariantMap PullController::fit() const {
    const auto f = calibration_.fit();
    return {{QStringLiteral("enoughData"), f.enoughData},
            {QStringLiteral("used"), static_cast<int>(f.used)},
            {QStringLiteral("rmsBeforeMm"), f.rmsBeforeMm},
            {QStringLiteral("rmsAfterMm"), f.rmsAfterMm},
            {QStringLiteral("gain"), f.gain},
            {QStringLiteral("compression"), f.compression},
            {QStringLiteral("weftSoftness"), f.weftSoftness},
            {QStringLiteral("message"), q(f.message)}};
}

QString PullController::limits() const {
    return QStringLiteral(
        "Estimation relative d'un modèle empirique, non calibrée sur machine : utile pour comparer "
        "deux versions d'un motif, pas pour garantir un résultat physique.");
}

void PullController::say(bool ok, const QString& text) {
    ok_ = ok;
    message_ = text;
    emit messageChanged();
}

bool PullController::setMode(int mode) {
    if (inspector_ == nullptr || mode < 0 || mode > 3) {
        say(false, QStringLiteral("Mode de compensation inconnu."));
        return false;
    }
    const auto error = inspector_->setParam(QStringLiteral("pull_mode"), mode);
    say(error.isEmpty(), error.isEmpty()
                             ? QStringLiteral("Mode de compensation : %1").arg(modes().value(mode))
                             : error);
    emit changed();
    return error.isEmpty();
}

bool PullController::setFabric(const QString& name) {
    const bool ok = calibration_.setFabric(name.toStdString());
    say(ok, ok ? QStringLiteral("Tissu : %1").arg(name) : QStringLiteral("Tissu inconnu."));
    emit changed();
    return ok;
}

bool PullController::setMeasure(const QString& id, double millimeters) {
    const bool ok = calibration_.setMeasure(id.toStdString(), millimeters);
    say(ok, ok ? QStringLiteral("Mesure enregistrée.")
               : QStringLiteral("Mesure refusée : identifiant inconnu ou valeur invalide."));
    emit changed();
    return ok;
}

bool PullController::clearMeasure(const QString& id) {
    const bool ok = calibration_.clearMeasure(id.toStdString());
    say(ok, ok ? QStringLiteral("Mesure effacée.") : QStringLiteral("Identifiant inconnu."));
    emit changed();
    return ok;
}

void PullController::clearMeasures() {
    calibration_.clearMeasures();
    say(true, QStringLiteral("Mesures effacées."));
    emit changed();
}

bool PullController::importCalibration(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        say(false, QStringLiteral("Lecture impossible : %1").arg(path));
        return false;
    }
    const auto count = calibration_.applyCsv(QString::fromUtf8(file.readAll()).toStdString());
    say(count > 0, count > 0 ? QStringLiteral("%1 mesures importées.").arg(count)
                             : QStringLiteral("Aucune mesure reconnue dans ce fichier."));
    emit changed();
    return count > 0;
}

bool PullController::exportCalibration(const QString& path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        say(false, QStringLiteral("Écriture impossible : %1").arg(path));
        return false;
    }
    file.write(QByteArray::fromStdString(calibration_.csv()));
    const bool ok = file.commit();
    say(ok, ok ? QStringLiteral("Données de calibration exportées.")
               : QStringLiteral("Écriture impossible : %1").arg(path));
    return ok;
}

bool PullController::writeTestPiece(const QString& path) {
    const auto result = calibration_.writeTestPiece(path.toStdString());
    say(static_cast<bool>(result),
        result ? QStringLiteral("Pièce-test écrite.") : q(result.error().message));
    return static_cast<bool>(result);
}

bool PullController::compare() {
    const auto c = compareProjectPull(studio_.session().project(), calibration_.fabric());
    comparison_ = {{QStringLiteral("ok"), c.ok},
                   {QStringLiteral("summary"), q(c.summary())},
                   {QStringLiteral("points"), static_cast<int>(c.points)},
                   {QStringLiteral("maxShiftMm"), c.maxShiftMm},
                   {QStringLiteral("meanShiftMm"), c.meanShiftMm},
                   {QStringLiteral("nominalWidthMm"), c.nominalWidthMm},
                   {QStringLiteral("nominalHeightMm"), c.nominalHeightMm},
                   {QStringLiteral("predictedWidthMm"), c.predictedWidthMm},
                   {QStringLiteral("predictedHeightMm"), c.predictedHeightMm},
                   {QStringLiteral("calibrated"), c.calibrated}};
    say(c.ok, c.ok ? q(c.summary()) : q(c.message));
    emit changed();
    return c.ok;
}

bool PullController::registrationStale() const {
    return registrationRevision_ && *registrationRevision_ != studio_.session().revision();
}

bool PullController::canExportCompensated() const {
    return !compensatedDst_.empty() && !registrationStale();
}

void PullController::predictRegistration(double strength) {
    if (busy_) {
        return;
    }
    strength = std::clamp(strength, 0.0, 1.0);
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    busy_ = true;
    emit changed();
    project_ops::ColorRegistrationOptions options;
    options.compensate = strength > 0.0;
    options.strength = strength;
    tasks_.submit<project_ops::ColorRegistrationReport>(
        "studio.color_registration",
        [snapshot, options](const TaskContext&) {
            return project_ops::predict_color_registration(*snapshot, options);
        },
        [this, revision](Result<project_ops::ColorRegistrationReport>&& result) {
            busy_ = false;
            if (!result) {
                registration_ = {};
                compensatedDst_.clear();
                registrationRevision_.reset();
                say(false, q(result.error().message));
            } else {
                registration_ = {
                    {QStringLiteral("text"), q(result->text)},
                    {QStringLiteral("compensated"), !result->compensated.commands.empty()},
                    {QStringLiteral("dstBytes"), static_cast<int>(result->compensated_dst.size())}};
                compensatedDst_ = std::move(result->compensated_dst);
                registrationRevision_ = revision;
                say(true, QStringLiteral("Décalage entre couleurs prédit (estimation non "
                                         "calibrée)."));
            }
            emit changed();
        });
}

bool PullController::exportCompensatedDst(const QString& path) {
    if (registrationStale()) {
        say(false, QStringLiteral("Le motif a changé depuis la prédiction : relancez-la avant "
                                  "d'exporter."));
        return false;
    }
    if (compensatedDst_.empty()) {
        say(false, QStringLiteral("Aucune séquence compensée : lancez une prédiction avec "
                                  "contre-compensation."));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        say(false, QStringLiteral("Écriture impossible : %1").arg(path));
        return false;
    }
    file.write(reinterpret_cast<const char*>(compensatedDst_.data()),
               static_cast<qint64>(compensatedDst_.size()));
    const bool ok = file.commit();
    say(ok, ok ? QStringLiteral("DST compensé écrit.") : QStringLiteral("Écriture impossible."));
    return ok;
}

} // namespace openstitch::desktop_v2::studio
