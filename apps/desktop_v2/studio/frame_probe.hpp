// SPDX-License-Identifier: Apache-2.0
#pragma once

// Sonde de frames de la VRAIE fenêtre Studio (QML, panneaux, canevas), activée par
// `--frame-stats <json>`. Le banc `openstitch-studio-bench` isole le canevas ; celle-ci mesure
// l'application entière : intervalle entre frames présentées, temps de synchronisation (fil
// d'interface bloqué par le rendu), temps de rendu CPU, et blocages du fil d'interface (minuteur
// témoin). Un scénario optionnel (`pan`, `idle`) génère du mouvement pour que la boucle de rendu
// produise des frames en continu.

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class QQuickWindow;

namespace openstitch::desktop_v2::studio {

class GpuCanvas;

class FrameProbe : public QObject {
public:
    FrameProbe(QQuickWindow& window, QString outputPath, QString scenario, int seconds,
               QObject* parent = nullptr);

    // Scenario `interact` : actions executees l'une apres l'autre sur le fil d'interface, chacune
    // suivie de `settleMs` d'observation. Mesure par action : cout synchrone, delai jusqu'a la
    // frame presentee suivante, jusqu'a la scene de points publiee, blocage max du fil d'interface.
    void addStep(QString name, std::function<void()> run, int settleMs);
    // A appeler quand une scene de points est publiee (StudioController::sceneChanged).
    void noteSceneReady();

    // Lance la mesure (après chargement de la scène). Quitte l'application à la fin.
    void start(GpuCanvas* canvas);

private:
    void finish();
    void runStep(std::size_t index);
    [[nodiscard]] QJsonArray analyseSteps() const;

    struct Step {
        QString name;
        std::function<void()> run;
        int settleMs{0};
        double startMs{0};
        double syncCostMs{0};
    };
    std::vector<Step> steps_;
    std::vector<double> sceneReady_; // ms depuis le debut
    std::vector<double> stallTimes_; // ms depuis le debut, parallele a stalls_

    QQuickWindow& window_;
    QString output_;
    QString scenario_;
    int seconds_;
    GpuCanvas* canvas_{nullptr};
    QElapsedTimer clock_;
    QTimer heartbeat_;
    QTimer driver_;
    std::mutex mutex_;
    std::vector<double> swaps_;  // ms depuis le début, thread de rendu
    std::vector<double> sync_;   // ms : beforeSynchronizing -> afterSynchronizing
    std::vector<double> render_; // ms : beforeRendering -> afterRendering
    std::vector<double> stalls_; // ms de retard du minuteur témoin (fil d'interface)
    qint64 syncStart_{0};
    qint64 renderStart_{0};
    std::atomic<qint64> lastBeat_{0};
    // Echantillonneur de pile du fil d'interface (Windows) : quand le minuteur temoin a plus de
    // 60 ms de retard, capture la pile pour dire QUOI bloque (pas seulement combien de temps).
    void startSampler();
    [[nodiscard]] QJsonArray symbolizeSamples();
    std::jthread sampler_;
    std::vector<std::vector<std::uintptr_t>> samples_;
    void* guiThread_{nullptr};
    std::uint64_t driverTick_{0};
    bool running_{false};
};

} // namespace openstitch::desktop_v2::studio
