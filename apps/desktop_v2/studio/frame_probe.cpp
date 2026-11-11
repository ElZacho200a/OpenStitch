// SPDX-License-Identifier: Apache-2.0
#include "frame_probe.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "gpu_canvas.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// dbghelp.h doit suivre windows.h
#include <dbghelp.h>
#include <map>
#endif

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

void FrameProbe::addStep(QString name, std::function<void()> run, int settleMs) {
    steps_.push_back({std::move(name), std::move(run), settleMs, 0, 0});
}

void FrameProbe::noteSceneReady() {
    if (running_) {
        qInfo("FrameProbe: t=%.0f scene prete", nowMs(clock_));
        sceneReady_.push_back(nowMs(clock_));
    }
}

void FrameProbe::runStep(std::size_t index) {
    if (index >= steps_.size()) {
        finish();
        return;
    }
    auto& step = steps_[index];
    qInfo("FrameProbe: t=%.0f etape %s", nowMs(clock_), qPrintable(step.name));
    step.startMs = nowMs(clock_);
    step.run();
    step.syncCostMs = nowMs(clock_) - step.startMs;
    qInfo("FrameProbe: t=%.0f etape %s rendue en %.1f ms", nowMs(clock_), qPrintable(step.name),
          step.syncCostMs);
    QTimer::singleShot(step.settleMs, this, [this, index] { runStep(index + 1); });
}

QJsonArray FrameProbe::analyseSteps() const {
    QJsonArray out;
    for (const auto& step : steps_) {
        const double begin = step.startMs;
        const double end = begin + step.settleMs;
        QJsonObject row;
        row["step"] = step.name;
        row["sync_cost_ms"] = step.syncCostMs;
        double firstFrame = -1;
        double maxInterval = 0;
        int frames = 0;
        for (std::size_t k = 0; k < swaps_.size(); ++k) {
            if (swaps_[k] < begin || swaps_[k] > end) {
                continue;
            }
            ++frames;
            if (firstFrame < 0) {
                firstFrame = swaps_[k] - begin;
            }
            if (k > 0 && swaps_[k - 1] >= begin) {
                maxInterval = std::max(maxInterval, swaps_[k] - swaps_[k - 1]);
            }
        }
        row["to_first_frame_ms"] = firstFrame;
        row["frames_in_window"] = frames;
        row["max_frame_interval_ms"] = maxInterval;
        double scene = -1;
        for (const double t : sceneReady_) {
            if (t >= begin && t <= end) {
                scene = t - begin;
                break;
            }
        }
        row["to_scene_ready_ms"] = scene;
        double maxStall = 0;
        for (std::size_t k = 0; k < stalls_.size() && k < stallTimes_.size(); ++k) {
            if (stallTimes_[k] >= begin && stallTimes_[k] <= end) {
                maxStall = std::max(maxStall, stalls_[k]);
            }
        }
        row["gui_stall_max_ms"] = maxStall;
        out.append(row);
    }
    return out;
}

#ifdef _WIN32
void FrameProbe::startSampler() {
    HANDLE duplicate = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &duplicate, 0,
                    FALSE, DUPLICATE_SAME_ACCESS);
    guiThread_ = duplicate;
    if (!duplicate) {
        return;
    }
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    const QByteArray searchPath = QCoreApplication::applicationDirPath().toLocal8Bit();
    SymInitialize(GetCurrentProcess(), searchPath.constData(), TRUE);
    sampler_ = std::jthread([this, duplicate](std::stop_token stop) {
        std::vector<unsigned char> stackCopy(262144); // alloue AVANT toute suspension
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (clock_.nsecsElapsed() - lastBeat_.load() < 60'000'000) {
                continue; // le fil d'interface est vivant : rien a echantillonner
            }
            // Pendant la suspension : AUCUNE allocation (le fil d'interface peut tenir le verrou
            // du tas) -> on copie seulement la pile brute dans un tampon deja alloue.
            CONTEXT context{};
            context.ContextFlags = CONTEXT_FULL;
            if (SuspendThread(duplicate) == static_cast<DWORD>(-1)) {
                continue;
            }
            std::size_t copied = 0;
            const bool haveContext = GetThreadContext(duplicate, &context) != 0;
            if (haveContext) {
                for (const std::size_t size : {std::size_t{262144}, std::size_t{65536},
                                               std::size_t{16384}, std::size_t{4096}}) {
                    SIZE_T got = 0;
                    if (ReadProcessMemory(GetCurrentProcess(),
                                          reinterpret_cast<LPCVOID>(context.Rsp), stackCopy.data(),
                                          size, &got) &&
                        got == size) {
                        copied = size;
                        break;
                    }
                }
            }
            ResumeThread(duplicate);
            if (!haveContext || copied == 0) {
                continue;
            }
            // Depliage hors suspension, sur la copie de pile.
            struct Source {
                const unsigned char* data;
                DWORD64 base;
                std::size_t size;
            };
            static thread_local Source source{};
            source = {stackCopy.data(), context.Rsp, copied};
            const auto readMemory = [](HANDLE, DWORD64 address, PVOID buffer, DWORD size,
                                       LPDWORD read) -> BOOL {
                if (address >= source.base && address + size <= source.base + source.size) {
                    std::memcpy(buffer, source.data + (address - source.base), size);
                    *read = size;
                    return TRUE;
                }
                SIZE_T got = 0;
                const BOOL ok = ReadProcessMemory(
                    GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), buffer, size, &got);
                *read = static_cast<DWORD>(got);
                return ok;
            };
            std::vector<std::uintptr_t> frames;
            STACKFRAME64 frame{};
            frame.AddrPC.Offset = context.Rip;
            frame.AddrFrame.Offset = context.Rbp;
            frame.AddrStack.Offset = context.Rsp;
            frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
            frames.push_back(static_cast<std::uintptr_t>(context.Rip));
            while (frames.size() < 28 &&
                   StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), duplicate, &frame,
                               &context, readMemory, SymFunctionTableAccess64, SymGetModuleBase64,
                               nullptr)) {
                if (frame.AddrPC.Offset == 0) {
                    break;
                }
                frames.push_back(static_cast<std::uintptr_t>(frame.AddrPC.Offset));
            }
            samples_.push_back(std::move(frames));
        }
    });
}

QJsonArray FrameProbe::symbolizeSamples() {
    sampler_.request_stop();
    if (sampler_.joinable()) {
        sampler_.join();
    }
    const auto nameOf = [](std::uintptr_t address) {
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(GetCurrentProcess(), address, &displacement, symbol)) {
            return QString::fromLatin1(static_cast<const char*>(symbol->Name));
        }
        HMODULE module = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(address), &module)) {
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(module, path, MAX_PATH);
            const auto name = QString::fromWCharArray(path).section(QLatin1Char('\\'), -1);
            return QStringLiteral("%1+0x%2").arg(name).arg(
                address - reinterpret_cast<std::uintptr_t>(module), 0, 16);
        }
        return QStringLiteral("?");
    };
    // Cle = les 8 premieres frames symbolisees ; on regroupe les echantillons identiques.
    std::map<QString, std::pair<int, QStringList>> groups;
    for (const auto& frames : samples_) {
        QStringList names;
        for (std::size_t i = 0; i < frames.size() && i < 14; ++i) {
            names.push_back(nameOf(frames[i]));
        }
        const QString key = names.mid(0, 8).join(QLatin1Char('|'));
        auto& slot = groups[key];
        ++slot.first;
        if (slot.second.isEmpty()) {
            slot.second = names;
        }
    }
    std::vector<std::pair<int, QStringList>> sorted;
    for (const auto& [key, value] : groups) {
        (void)key;
        sorted.push_back(value);
    }
    std::ranges::sort(sorted, [](const auto& a, const auto& b) { return a.first > b.first; });
    QJsonArray out;
    for (std::size_t i = 0; i < sorted.size() && i < 12; ++i) {
        QJsonObject row;
        row["samples"] = sorted[i].first;
        row["stack"] = QJsonArray::fromStringList(sorted[i].second);
        out.append(row);
    }
    return out;
}
#else
void FrameProbe::startSampler() {}
QJsonArray FrameProbe::symbolizeSamples() {
    return {};
}
#endif

void FrameProbe::start(GpuCanvas* canvas) {
    if (running_) {
        return;
    }
    running_ = true;
    qInfo("FrameProbe: demarrage, scenario=%s, etapes=%d", qPrintable(scenario_),
          static_cast<int>(steps_.size()));
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
        stallTimes_.push_back(nowMs(clock_));
    });
    heartbeat_.start();
    if (qEnvironmentVariableIsSet("OPENSTITCH_FRAME_SAMPLER")) {
        startSampler();
    }
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
    if (scenario_ == QStringLiteral("interact")) {
        QTimer::singleShot(300, this, [this] { runStep(0); });
        return;
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
    if (scenario_ == QStringLiteral("interact")) {
        root["interactions"] = analyseSteps();
    }
    if (qEnvironmentVariableIsSet("OPENSTITCH_FRAME_SAMPLER")) {
        root["stall_stacks"] = symbolizeSamples();
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
