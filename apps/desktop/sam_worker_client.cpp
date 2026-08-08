// SPDX-License-Identifier: Apache-2.0
#include "sam_worker_client.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimer>
#include <utility>

#include "wsl_path_converter.hpp"

namespace openstitch::desktop {

namespace {

QString toQString(std::string_view sv) {
    return QString::fromUtf8(sv.data(), static_cast<qsizetype>(sv.size()));
}

}  // namespace

SamWorkerClient::SamWorkerClient(QObject* parent) : QObject(parent) {
    watchdogTimer_ = new QTimer(this);
    watchdogTimer_->setSingleShot(true);
    connect(watchdogTimer_, &QTimer::timeout, this, &SamWorkerClient::onWatchdogTimeout);

    stabilityTimer_ = new QTimer(this);
    stabilityTimer_->setSingleShot(true);
    connect(stabilityTimer_, &QTimer::timeout, this, &SamWorkerClient::onStabilityTimeout);
}

SamWorkerClient::~SamWorkerClient() {
    if (process_ != nullptr && process_->state() != QProcess::NotRunning) {
        stoppingIntentionally_ = true;
        process_->kill();
        process_->waitForFinished(500);
    }
}

void SamWorkerClient::configure(SamWorkerConfig config) {
    config_ = std::move(config);
}

bool SamWorkerClient::isConfigured() const {
    if (config_.pythonExecutable.isEmpty() || config_.workerScriptPath.isEmpty() ||
        config_.modelsDir.isEmpty()) {
        return false;
    }
    if (config_.runtime == AiRuntimeKind::WslPython && config_.wslDistro.isEmpty()) {
        return false;
    }
    return true;
}

void SamWorkerClient::setState(State s) {
    if (state_ == s) {
        return;
    }
    state_ = s;
    if (s == State::Ready) {
        stabilityTimer_->start(kStabilityTimeoutMs);
    }
    emit stateChanged(s);
}

void SamWorkerClient::onStabilityTimeout() {
    crashRestartCount_ = 0;
}

QString SamWorkerClient::translateForWorker(const QString& windowsPath) const {
    if (config_.runtime == AiRuntimeKind::WslPython) {
        return WslPathConverter::toWsl(windowsPath);
    }
    return windowsPath;
}

void SamWorkerClient::launchProcess() {
    if (process_ == nullptr) {
        process_ = new QProcess(this);
        process_->setProcessChannelMode(QProcess::SeparateChannels);
        connect(process_, &QProcess::readyReadStandardOutput, this,
                &SamWorkerClient::onReadyReadStandardOutput);
        connect(process_, &QProcess::readyReadStandardError, this,
                &SamWorkerClient::onReadyReadStandardError);
        connect(process_, &QProcess::errorOccurred, this, &SamWorkerClient::onProcessErrorOccurred);
        connect(process_, &QProcess::finished, this, &SamWorkerClient::onProcessFinished);
    }

    QString program;
    QStringList arguments;
    if (config_.runtime == AiRuntimeKind::WslPython) {
        program = QStringLiteral("wsl.exe");
        arguments << QStringLiteral("-d") << config_.wslDistro << QStringLiteral("--")
                  << config_.pythonExecutable << config_.workerScriptPath << QStringLiteral("--models-dir")
                  << translateForWorker(config_.modelsDir) << QStringLiteral("--device") << config_.device;
    } else {
        program = config_.pythonExecutable;
        arguments << config_.workerScriptPath << QStringLiteral("--models-dir") << config_.modelsDir
                  << QStringLiteral("--device") << config_.device;
    }
    process_->setProgram(program);
    process_->setArguments(arguments);
}

void SamWorkerClient::start() {
    if (!isConfigured()) {
        setState(State::Unavailable);
        emit workerError(
            {}, ai_segmentation::AiErrorCode::WorkerNotConfigured,
            toQString(ai_segmentation::default_message(ai_segmentation::AiErrorCode::WorkerNotConfigured)), {});
        return;
    }
    if (process_ != nullptr && process_->state() != QProcess::NotRunning) {
        return;  // déjà démarré
    }
    stoppingIntentionally_ = false;
    setState(State::Starting);
    launchProcess();
    process_->start();
}

void SamWorkerClient::stop() {
    watchdogTimer_->stop();
    stabilityTimer_->stop();
    if (process_ == nullptr || process_->state() == QProcess::NotRunning) {
        setState(State::NotConfigured);
        return;
    }
    stoppingIntentionally_ = true;
    QJsonObject request;
    request["id"] = newRequestId();
    request["type"] = QStringLiteral("shutdown");
    sendRequest(request);
    if (!process_->waitForFinished(3000)) {
        process_->kill();
        process_->waitForFinished(1000);
    }
    setState(State::NotConfigured);
}

QString SamWorkerClient::newRequestId() {
    return QStringLiteral("req-%1").arg(nextRequestId_++);
}

void SamWorkerClient::sendRequest(const QJsonObject& request) {
    if (process_ == nullptr || process_->state() != QProcess::Running) {
        emit workerError(
            request.value(QStringLiteral("id")).toString(), ai_segmentation::AiErrorCode::WorkerNotConfigured,
            toQString(ai_segmentation::default_message(ai_segmentation::AiErrorCode::WorkerNotConfigured)), {});
        return;
    }
    const QString type = request.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("load_model") || type == QStringLiteral("segment_image")) {
        watchdogTimer_->start(kWatchdogTimeoutMs);
    }
    const QByteArray line = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    process_->write(line);
}

QString SamWorkerClient::loadModel(ai_segmentation::ModelId modelId) {
    const auto& descriptor = ai_segmentation::model_descriptor(modelId);
    const QString requestId = newRequestId();
    setState(State::LoadingModel);
    QJsonObject request;
    request["id"] = requestId;
    request["type"] = QStringLiteral("load_model");
    request["model"] = toQString(descriptor.worker_id);
    sendRequest(request);
    return requestId;
}

QString SamWorkerClient::segmentImage(const QString& jobDirWindows, const QString& imageFile,
                                      const SamSegmentParams& params) {
    const QString requestId = newRequestId();
    setState(State::Segmenting);
    QJsonObject request;
    request["id"] = requestId;
    request["type"] = QStringLiteral("segment_image");
    request["job_dir"] = translateForWorker(jobDirWindows);
    request["image_file"] = imageFile;
    request["profile"] = params.profile;
    if (params.pointsPerSide) {
        request["points_per_side"] = *params.pointsPerSide;
    }
    if (params.predIouThresh) {
        request["pred_iou_thresh"] = *params.predIouThresh;
    }
    if (params.stabilityScoreThresh) {
        request["stability_score_thresh"] = *params.stabilityScoreThresh;
    }
    if (params.minMaskRegionArea) {
        request["min_mask_region_area"] = *params.minMaskRegionArea;
    }
    request["fill_hole_area_threshold"] = params.fillHoleAreaThreshold;
    request["remove_island_area_threshold"] = params.removeIslandAreaThreshold;
    if (params.maxResolution) {
        request["max_resolution"] = *params.maxResolution;
    }
    sendRequest(request);
    return requestId;
}

void SamWorkerClient::cancel(const QString& requestId) {
    setState(State::Cancelling);
    QJsonObject request;
    request["id"] = newRequestId();
    request["type"] = QStringLiteral("cancel");
    request["target_id"] = requestId;
    sendRequest(request);
}

void SamWorkerClient::onReadyReadStandardOutput() {
    stdoutBuffer_ += process_->readAllStandardOutput();
    qsizetype newlineIndex = -1;
    while ((newlineIndex = stdoutBuffer_.indexOf('\n')) >= 0) {
        const QByteArray line = stdoutBuffer_.left(newlineIndex);
        stdoutBuffer_.remove(0, newlineIndex + 1);
        handleLine(line);
    }
}

void SamWorkerClient::onReadyReadStandardError() {
    stderrLog_ += QString::fromUtf8(process_->readAllStandardError());
    constexpr qsizetype kMaxStderrLog = 64 * 1024;
    if (stderrLog_.size() > kMaxStderrLog) {
        stderrLog_.remove(0, stderrLog_.size() - kMaxStderrLog);
    }
}

void SamWorkerClient::handleLine(const QByteArray& line) {
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit workerError({}, ai_segmentation::AiErrorCode::InvalidWorkerResponse,
                         toQString(ai_segmentation::default_message(
                             ai_segmentation::AiErrorCode::InvalidWorkerResponse)),
                         QString::fromUtf8(trimmed));
        return;
    }
    handleMessage(doc.object());
}

void SamWorkerClient::handleMessage(const QJsonObject& message) {
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("worker_starting")) {
        return;
    }
    if (type == QStringLiteral("worker_ready")) {
        setState(State::Ready);
        return;
    }
    if (type == QStringLiteral("pong") || type == QStringLiteral("cancel_acknowledged") ||
        type == QStringLiteral("shutting_down")) {
        return;
    }
    if (type == QStringLiteral("progress")) {
        watchdogTimer_->start(kWatchdogTimeoutMs);
        emit progress(message.value(QStringLiteral("id")).toString(), message.value(QStringLiteral("stage")).toString());
        return;
    }
    if (type == QStringLiteral("model_ready")) {
        watchdogTimer_->stop();
        setState(State::Ready);
        emit modelReady(message.value(QStringLiteral("model")).toString(),
                        message.value(QStringLiteral("device")).toString(),
                        message.value(QStringLiteral("load_seconds")).toDouble());
        return;
    }
    if (type == QStringLiteral("segment_result")) {
        watchdogTimer_->stop();
        setState(State::Ready);
        emit segmentResult(message.value(QStringLiteral("id")).toString(),
                           message.value(QStringLiteral("job_dir")).toString(),
                           message.value(QStringLiteral("masks_file")).toString(),
                           message.value(QStringLiteral("mask_count")).toInt());
        return;
    }
    if (type == QStringLiteral("cancelled")) {
        watchdogTimer_->stop();
        setState(State::Ready);
        emit requestCancelled(message.value(QStringLiteral("target_id")).toString());
        return;
    }
    if (type == QStringLiteral("error")) {
        watchdogTimer_->stop();
        setState(State::Ready);
        const auto code =
            ai_segmentation::ai_error_code_from_name(message.value(QStringLiteral("code")).toString().toStdString());
        emit workerError(message.value(QStringLiteral("id")).toString(), code,
                         message.value(QStringLiteral("message")).toString(),
                         message.value(QStringLiteral("details")).toString());
        return;
    }
    emit workerError({}, ai_segmentation::AiErrorCode::InvalidWorkerResponse,
                     toQString(ai_segmentation::default_message(ai_segmentation::AiErrorCode::InvalidWorkerResponse)),
                     QStringLiteral("type inconnu : ") + type);
}

void SamWorkerClient::onProcessErrorOccurred(QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart) {
        return;  // les autres cas sont traites par onProcessFinished()
    }
    setState(State::Unavailable);
    emit workerError({}, ai_segmentation::AiErrorCode::WorkerStartFailed,
                     toQString(ai_segmentation::default_message(ai_segmentation::AiErrorCode::WorkerStartFailed)),
                     process_->errorString());
}

void SamWorkerClient::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    watchdogTimer_->stop();
    stabilityTimer_->stop();
    if (stoppingIntentionally_) {
        stoppingIntentionally_ = false;
        setState(State::NotConfigured);
        return;
    }
    const QString detail = QStringLiteral("code=%1 status=%2")
                               .arg(exitCode)
                               .arg(exitStatus == QProcess::CrashExit ? QStringLiteral("crash")
                                                                      : QStringLiteral("exit"));
    emit crashed(detail);
    setState(State::Crashed);
    if (crashRestartCount_ < kMaxAutoRestarts) {
        ++crashRestartCount_;
        setState(State::Starting);
        launchProcess();
        process_->start();
    } else {
        setState(State::Unavailable);
    }
}

void SamWorkerClient::onWatchdogTimeout() {
    if (process_ == nullptr || process_->state() == QProcess::NotRunning) {
        return;
    }
    // Le worker ne repond plus depuis kWatchdogTimeoutMs : le considerer
    // plante -- kill() declenche onProcessFinished(), qui gere la
    // reconnexion/redemarrage comme pour tout autre crash.
    process_->kill();
}

}  // namespace openstitch::desktop
