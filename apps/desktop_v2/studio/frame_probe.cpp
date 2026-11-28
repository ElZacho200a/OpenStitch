// SPDX-License-Identifier: Apache-2.0
#include "frame_probe.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QQuickWindow>
#include <algorithm>
#include <cmath>

#include "gpu_canvas.hpp"
#include "studio_controller.hpp"

#include <functional>

namespace openstitch::desktop_v2::studio {

namespace {
double nowMs(const QElapsedTimer& clock) {
    return static_cast<double>(clock.nsecsElapsed()) / 1.0e6;
}

QJsonObject distribution(std::vector<double> values) {
    QJsonObject out;
    out["count"] = static_cast<qint64>(values.size());
    if (values.empty()) {
        return out;
    }
    std::ranges::sort(values);
    const auto at = [&](double q) {
        return values[static_cast<std::size_t>(q * static_cast<double>(values.size() - 1))];
    };
    out["median"] = at(0.5);
    out["p95"] = at(0.95);
    out["p99"] = at(0.99);
    out["max"] = values.back();
    return out;
}
} // namespace

FrameProbe::FrameProbe(QQuickWindow& window, QString outputPath, QString scenario, int seconds,
                       QObject* parent)
    : QObject(parent), window_(window), output_(std::move(outputPath)),
      scenario_(std::move(scenario)), seconds_(seconds) {}

void FrameProbe::start(GpuCanvas* canvas) {
    if (running_) {
        return;
    }
    running_ = true;
    canvas_ = canvas;
    clock_.start();
    // Signaux émis sur le fil de rendu (boucle « threaded ») : connexion directe + verrou.
    connect(
        &window_, &QQuickWindow::beforeSynchronizing, this,
        [this] { syncStart_ = clock_.nsecsElapsed(); }, Qt::DirectConnection);
    connect(
        &window_, &QQuickWindow::afterSynchronizing, this,
        [this] {
            std::scoped_lock lock(mutex_);
            sync_.push_back(static_cast<double>(clock_.nsecsElapsed() - syncStart_) / 1.0e6);
        },
        Qt::DirectConnection);
    connect(
        &window_, &QQuickWindow::beforeRendering, this,
        [this] { renderStart_ = clock_.nsecsElapsed(); }, Qt::DirectConnection);
    connect(
        &window_, &QQuickWindow::afterRendering, this,
        [this] {
            std::scoped_lock lock(mutex_);
            render_.push_back(static_cast<double>(clock_.nsecsElapsed() - renderStart_) / 1.0e6);
        },
        Qt::DirectConnection);
    connect(
        &window_, &QQuickWindow::frameSwapped, this,
        [this] {
            std::scoped_lock lock(mutex_);
            swaps_.push_back(nowMs(clock_));
        },
        Qt::DirectConnection);
    // Minuteur témoin : un fil d'interface libre le rappelle à ~4 ms ; tout retard est un blocage.
    constexpr int beatMs = 4;
    heartbeat_.setTimerType(Qt::PreciseTimer);
    heartbeat_.setInterval(beatMs);
    lastBeat_ = clock_.nsecsElapsed();
    connect(&heartbeat_, &QTimer::timeout, this, [this] {
        const auto now = clock_.nsecsElapsed();
        const double late = static_cast<double>(now - lastBeat_) / 1.0e6 - beatMs;
        lastBeat_ = now;
        stalls_.push_back(std::max(0.0, late));
    });
    heartbeat_.start();
    if (scenario_ == QStringLiteral("pan") && canvas_) {
        // Aller-retour de panoramique + léger zoom, une fois par frame (~60 Hz) : un geste continu.
        driver_.setTimerType(Qt::PreciseTimer);
        driver_.setInterval(16);
        connect(&driver_, &QTimer::timeout, this, [this] {
            const double phase = static_cast<double>(driverTick_++) * 0.05;
            canvas_->panBy(std::cos(phase) * 6.0, std::sin(phase) * 4.0);
            canvas_->zoomAt(1.0 + std::sin(phase * 0.5) * 0.004, canvas_->width() / 2.0,
                            canvas_->height() / 2.0);
        });
        driver_.start();
    }
    const bool clicks =
        scenario_ == QStringLiteral("click") || scenario_ == QStringLiteral("click-jitter");
    const bool deletes = scenario_ == QStringLiteral("delete");
    // OPENSTITCH_PROBE_THREAD_PAGE=order (ou film, stock…) : mesurer avec le panneau Fils ouvert
    // sur cette page, dont les listes suivent chaque édition.
    if (const auto page = qEnvironmentVariable("OPENSTITCH_PROBE_THREAD_PAGE"); !page.isEmpty()) {
        if (auto* hub = window_.findChild<QObject*>(QStringLiteral("viewHub"))) {
            hub->setProperty("openPanel", QStringLiteral("threads"));
        }
        if (auto* threadPanel = window_.findChild<QObject*>(QStringLiteral("threadPanel"))) {
            threadPanel->setProperty("page", page);
        }
    }
    if ((scenario_ == QStringLiteral("select") || clicks || deletes) && canvas_ &&
        canvas_->controller()) {
        // Sélections successives des zones aux formes les plus détaillées (plus une très simple,
        // témoin) : mesure le temps bloquant de chaque sélection sur le fil d'interface.
        auto* controller = canvas_->controller();
        const auto& project = controller->session().project();
        std::vector<std::pair<std::size_t, std::uint64_t>> ranked;
        for (const auto& object : project.embroidery_objects) {
            std::size_t nodes = 0;
            if (const auto* shape = project.findObject(object.source_vector)) {
                for (const auto& set : shape->paths) {
                    nodes += set.outer.nodes.size();
                    for (const auto& hole : set.holes) {
                        nodes += hole.nodes.size();
                    }
                }
            }
            ranked.emplace_back(nodes, object.id.value);
        }
        std::ranges::sort(ranked, std::greater{});
        for (std::size_t i = 0; i < ranked.size() && i < 5; ++i) {
            selectTargets_.push_back(ranked[i]);
        }
        if (ranked.size() > 5) {
            selectTargets_.push_back(ranked.back());
        }
        driver_.setInterval(600);
        connect(&driver_, &QTimer::timeout, this, [this] {
            if (selectTargets_.empty() || !canvas_ || !canvas_->controller()) {
                return;
            }
            if (scenario_ == QStringLiteral("delete")) {
                // Tick pair : sélectionner puis Suppr (chemin de l'éditeur) ; tick impair :
                // annuler. Mesure le temps bloquant de chaque geste sur le fil d'interface ; le
                // travail différé (scène, listes) apparaît dans les frames et le journal perf.
                auto* controller = canvas_->controller();
                auto* editor = canvas_->editor();
                if (editor == nullptr) {
                    return;
                }
                const bool undo = (driverTick_ % 2) == 1;
                const auto [nodes, id] = selectTargets_[(driverTick_ / 2) % selectTargets_.size()];
                ++driverTick_;
                QElapsedTimer timer;
                timer.start();
                if (undo) {
                    controller->undo();
                } else {
                    controller->selectObject(QString::number(id));
                    editor->deleteSelection();
                }
                const double ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
                selections_.push_back({undo ? 0 : id, nodes, ms, !undo, 0});
                return;
            }
            const auto [nodes, id] = selectTargets_[driverTick_++ % selectTargets_.size()];
            QElapsedTimer timer;
            const auto revBefore = canvas_->controller()->session().revision();
            if (scenario_ == QStringLiteral("select")) {
                timer.start();
                canvas_->controller()->selectObject(QString::number(id));
            } else {
                // Vrai clic souris sur le contour de la forme (premier nœud), comme l'utilisateur.
                const auto& project = canvas_->controller()->session().project();
                const auto* object = project.findEmbroidery(ObjectId{id});
                const auto* shape = object ? project.findObject(object->source_vector) : nullptr;
                if (shape == nullptr || shape->paths.empty() ||
                    shape->paths[0].outer.nodes.empty()) {
                    return;
                }
                const auto pos = shape->paths[0].outer.nodes[0].pos;
                const auto s =
                    canvas_->camera().worldToScreen({static_cast<double>(pos.x.value) / 1000.0,
                                                     -static_cast<double>(pos.y.value) / 1000.0});
                const QPointF local = canvas_->mapToScene(QPointF(s.x, s.y));
                const auto send = [this](QEvent::Type type, QPointF at, Qt::MouseButtons buttons) {
                    QMouseEvent event(type, at, at, window_.mapToGlobal(at), Qt::LeftButton,
                                      buttons, Qt::NoModifier);
                    QCoreApplication::sendEvent(&window_, &event);
                };
                timer.start();
                send(QEvent::MouseButtonPress, local, Qt::LeftButton);
                if (scenario_ == QStringLiteral("click-jitter")) {
                    send(QEvent::MouseMove, local + QPointF(3.0, 2.0), Qt::LeftButton);
                    send(QEvent::MouseButtonRelease, local + QPointF(3.0, 2.0), Qt::NoButton);
                } else {
                    send(QEvent::MouseButtonRelease, local, Qt::NoButton);
                }
            }
            const double ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
            const bool edited = canvas_->controller()->session().revision() != revBefore;
            const auto& targets = canvas_->controller()->session().selection().targets();
            selections_.push_back({id, nodes, ms, edited, targets.empty() ? 0 : targets.back().id});
        });
        driver_.start();
    }
    QTimer::singleShot(seconds_ * 1000, this, [this] { finish(); });
}

void FrameProbe::finish() {
    heartbeat_.stop();
    driver_.stop();
    std::vector<double> intervals;
    QJsonObject root;
    {
        std::scoped_lock lock(mutex_);
        for (std::size_t i = 1; i < swaps_.size(); ++i) {
            intervals.push_back(swaps_[i] - swaps_[i - 1]);
        }
        root["frames_presented"] = static_cast<qint64>(swaps_.size());
        root["sync_ms"] = distribution(sync_);
        root["render_cpu_ms"] = distribution(render_);
    }
    const double seconds = static_cast<double>(seconds_);
    root["scenario"] = scenario_;
    root["seconds"] = seconds_;
    root["fps_average"] =
        static_cast<double>(intervals.size() + (intervals.empty() ? 0 : 1)) / seconds;
    root["frame_interval_ms"] = distribution(intervals);
    const auto slow = std::ranges::count_if(intervals, [](double v) { return v > 20.0; });
    root["frames_over_20ms"] = static_cast<qint64>(slow);
    root["gui_stall_ms"] = distribution(stalls_);
    root["gui_stalls_over_16ms"] =
        static_cast<qint64>(std::ranges::count_if(stalls_, [](double v) { return v > 16.0; }));
    root["gui_wakeups"] = static_cast<qint64>(stalls_.size());
    if (!selections_.empty()) {
        QJsonArray rows;
        std::vector<double> selectMs;
        for (const auto& s : selections_) {
            rows.append(QJsonObject{{"id", static_cast<qint64>(s.id)},
                                    {"nodes", static_cast<qint64>(s.nodes)},
                                    {"ms", s.ms},
                                    {"edited", s.edited},
                                    {"selected", static_cast<qint64>(s.selected)}});
            selectMs.push_back(s.ms);
        }
        root["selections"] = rows;
        root["select_sync_ms"] = distribution(selectMs);
    }
    root["backend"] = canvas_ ? canvas_->backendName() : QString();
    root["window_px"] = QString("%1x%2").arg(window_.width()).arg(window_.height());
    QFile file(output_);
    int code = 4;
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson());
        code = 0;
    }
    QCoreApplication::exit(code);
}

} // namespace openstitch::desktop_v2::studio
