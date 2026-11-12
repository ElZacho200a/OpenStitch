// SPDX-License-Identifier: Apache-2.0
#pragma once

// Sonde de frames de la VRAIE fenêtre Studio (QML, panneaux, canevas), activée par
// `--frame-stats <json>`. Le banc `openstitch-studio-bench` isole le canevas ; celle-ci mesure
// l'application entière : intervalle entre frames présentées, temps de synchronisation (fil
// d'interface bloqué par le rendu), temps de rendu CPU, et blocages du fil d'interface (minuteur
// témoin). Un scénario optionnel (`pan`, `idle`) génère du mouvement pour que la boucle de rendu
// produise des frames en continu.

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <mutex>
#include <vector>

class QQuickWindow;

namespace openstitch::desktop_v2::studio {

class GpuCanvas;

class FrameProbe : public QObject {
public:
    FrameProbe(QQuickWindow& window, QString outputPath, QString scenario, int seconds,
               QObject* parent = nullptr);

    // Lance la mesure (après chargement de la scène). Quitte l'application à la fin.
    void start(GpuCanvas* canvas);

private:
    void finish();

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
    qint64 lastBeat_{0};
    std::uint64_t driverTick_{0};
    bool running_{false};
};

} // namespace openstitch::desktop_v2::studio
