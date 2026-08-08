// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>

#include <optional>

#include "openstitch/ai_segmentation/error.hpp"
#include "openstitch/ai_segmentation/model_catalog.hpp"

class QJsonObject;
class QTimer;

namespace openstitch::desktop {

// Le worker SAM 2 tourne dans un interpréteur Python séparé (jamais chargé
// en process via pybind11, cf. CLAUDE.md). MVP : au moins WSL doit
// fonctionner ; NativePython reste prévu pour une machine sans WSL.
enum class AiRuntimeKind {
    WslPython,
    NativePython,
};

struct SamWorkerConfig {
    AiRuntimeKind runtime{AiRuntimeKind::WslPython};
    QString wslDistro;          // ex. "Ubuntu" — ignoré si runtime == NativePython
    QString pythonExecutable;   // python du venv worker, côté WSL ou natif selon `runtime`
    QString workerScriptPath;   // openstitch_sam_worker.py, côté WSL ou natif selon `runtime`
    // Dossier des modèles : un chemin WINDOWS, supposé aussi visible depuis
    // WSL sous /mnt/<lettre>/... (hypothèse MVP documentée dans
    // WslPathConverter) — jamais un chemin choisi indépendamment côté worker.
    QString modelsDir;
    QString device{QStringLiteral("auto")};  // "auto" | "cpu" | "cuda"
};

struct SamSegmentParams {
    QString profile{QStringLiteral("balanced")};  // "main_shapes" | "balanced" | "detail"
    std::optional<int> pointsPerSide;
    std::optional<double> predIouThresh;
    std::optional<double> stabilityScoreThresh;
    std::optional<int> minMaskRegionArea;
    int fillHoleAreaThreshold{0};
    int removeIslandAreaThreshold{0};
    std::optional<int> maxResolution;
};

// Client du worker de segmentation SAM 2 : démarre/surveille le processus,
// parle JSON Lines sur son stdin/stdout, expose une machine à états et des
// signaux Qt — jamais d'attente bloquante côté appelant (sauf `stop()`,
// bornée, appelée à la fermeture de l'application).
class SamWorkerClient : public QObject {
    Q_OBJECT

public:
    enum class State {
        NotConfigured,
        Starting,
        Ready,
        LoadingModel,
        Segmenting,
        Cancelling,
        Crashed,
        Unavailable,
    };
    Q_ENUM(State)

    explicit SamWorkerClient(QObject* parent = nullptr);
    ~SamWorkerClient() override;

    void configure(SamWorkerConfig config);
    [[nodiscard]] bool isConfigured() const;
    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] QString stderrLog() const { return stderrLog_; }

    // Démarre le processus worker s'il n'est pas déjà lancé. Asynchrone :
    // l'état passe à Ready quand `worker_ready` est reçu.
    void start();
    // Arrêt propre : demande `shutdown`, attend une durée bornée, force
    // l'arrêt sinon. Bloquant (borné) — réservé à la fermeture de l'appli.
    void stop();

    QString loadModel(ai_segmentation::ModelId modelId);
    QString segmentImage(const QString& jobDirWindows, const QString& imageFile,
                         const SamSegmentParams& params);
    void cancel(const QString& requestId);

signals:
    void stateChanged(State state);
    void modelReady(QString modelWorkerId, QString device, double loadSeconds);
    void progress(QString requestId, QString stage);
    void segmentResult(QString requestId, QString jobDir, QString masksFile, int maskCount);
    void requestCancelled(QString requestId);
    void workerError(QString requestId, openstitch::ai_segmentation::AiErrorCode code, QString message,
                     QString detail);
    void crashed(QString detail);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onWatchdogTimeout();
    void onStabilityTimeout();

private:
    void setState(State s);
    void sendRequest(const QJsonObject& request);
    void handleLine(const QByteArray& line);
    void handleMessage(const QJsonObject& message);
    [[nodiscard]] QString newRequestId();
    [[nodiscard]] QString translateForWorker(const QString& windowsPath) const;
    void launchProcess();

    static constexpr int kMaxAutoRestarts = 3;
    static constexpr int kWatchdogTimeoutMs = 180'000;
    // Un worker qui plante juste après avoir atteint Ready (ex. crash au tout
    // début d'une inférence) ne doit PAS remettre le compteur de
    // redémarrages à zéro à chaque cycle, sinon la boucle de plantage ne
    // s'arrête jamais : le compteur n'est remis à zéro qu'après une période
    // de stabilité réelle en Ready, pas dès l'entrée dans cet état.
    static constexpr int kStabilityTimeoutMs = 3'000;

    QProcess* process_{nullptr};
    QTimer* watchdogTimer_{nullptr};
    QTimer* stabilityTimer_{nullptr};
    SamWorkerConfig config_;
    State state_{State::NotConfigured};
    QByteArray stdoutBuffer_;
    QString stderrLog_;
    quint64 nextRequestId_{1};
    int crashRestartCount_{0};
    bool stoppingIntentionally_{false};
};

}  // namespace openstitch::desktop
