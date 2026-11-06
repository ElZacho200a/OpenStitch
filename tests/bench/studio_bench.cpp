// SPDX-License-Identifier: Apache-2.0
// Native-window benchmark, deliberately not a CTest or an offscreen FPS assertion.
#include <QApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPaintEvent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScopeGuard>
#include <QScreen>
#include <QSurfaceFormat>
#include <QSysInfo>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "digitize_pipeline.hpp"
#include "gpu_canvas.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "preview_canvas.hpp"
#include "preview_service.hpp"
#include "studio_scene.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
// clang-format off
#include <psapi.h>
// clang-format on
#endif

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;

double ms(Time from, Time to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

QJsonValue workingSetMb(bool peak) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        const auto bytes = peak ? counters.PeakWorkingSetSize : counters.WorkingSetSize;
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }
#else
    (void)peak;
#endif
    return QJsonValue::Null;
}

// A real editable running-stitch object: geometry is sampled by effective_sequence in both
// backends. A 1 mm unit grid keeps every leg exact, including row turns. emit_polyline emits
// a Jump followed by one Stitch per sampled node. The runtime verifies the requested count.
std::shared_ptr<const document::Project> synthetic(std::size_t count) {
    auto project = std::make_shared<document::Project>();
    project->finishing = document::SequenceFinishing::legacy();
    const auto cols = static_cast<std::size_t>(std::ceil(std::sqrt(double(count))));
    const auto rows = (count + cols - 1) / cols;
    project->canvas.width = Micrometers{static_cast<std::int32_t>((cols + 10) * 1000)};
    project->canvas.height = Micrometers{static_cast<std::int32_t>((rows + 10) * 1000)};
    document::VectorObject shape;
    shape.id = project->object_ids.next();
    shape.name = "Deterministic 1 mm snake";
    geometry::PathSet path;
    path.outer.closed = false;
    path.outer.nodes.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t row = i / cols;
        const std::size_t col = (row % 2 == 0) ? i % cols : cols - 1 - i % cols;
        const auto x =
            static_cast<std::int32_t>(col * 1000) - static_cast<std::int32_t>((cols - 1) * 500);
        const auto y =
            static_cast<std::int32_t>(row * 1000) - static_cast<std::int32_t>((rows - 1) * 500);
        path.outer.nodes.push_back({Vec2um{Micrometers{x}, Micrometers{y}}});
    }
    shape.paths.push_back(std::move(path));
    document::EmbroideryObject object;
    object.id = project->object_ids.next();
    object.source_vector = shape.id;
    object.rgb = {197, 63, 96};
    document::RunningStitchParams params;
    params.stitch_length = Micrometers{1000};
    params.min_length = Micrometers{1};
    object.params = params;
    project->vector_objects.push_back(std::move(shape));
    project->embroidery_objects.push_back(std::move(object));
    return project;
}

double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(std::ceil(p * double(values.size()))) - 1];
}

QJsonObject distribution(const std::vector<double>& values) {
    QJsonArray raw;
    for (double value : values) {
        raw.append(value);
    }
    return {{"median", percentile(values, 0.5)},
            {"p95", percentile(values, 0.95)},
            {"max", *std::max_element(values.begin(), values.end())},
            {"samples", raw}};
}

class PaintedCanvas : public PreviewCanvas {
public:
    std::function<void(Time)> painted;

protected:
    void paintEvent(QPaintEvent* event) override {
        PreviewCanvas::paintEvent(event);
        if (painted) {
            painted(Clock::now());
        }
    }
};

struct Options {
    QString backend{"quick"};
    QString project;
    QString output;
    QString scene{"all"};
    QString generateProject;
    QString image;
    double heightMm{300.0};
    int frames{240};
    int warmup{30};
};

// Each request waits for one completed paint/swap before the next camera gesture. Input latency
// starts immediately before the C++ camera mutation; it excludes the OS input-device pipeline.
// Quick's timestamp is captured on the render thread, before queuing its callback to the GUI.
QJsonObject run(const Options& options, const QString& name,
                std::shared_ptr<const document::Project> project, std::size_t requested,
                double loadMs) {
    QJsonObject result{{"scene", name}, {"load_ms", loadMs}};
    const auto sceneStart = Clock::now();
    auto built = build_studio_scene(project, 1, TaskContext{});
    const double sceneMs = ms(sceneStart, Clock::now());
    if (!built) {
        throw std::runtime_error(built.error().message);
    }
    const auto snapshot = *built;
    const auto sequence = stitch_generation::effective_sequence(*project);
    if (!sequence) {
        throw std::runtime_error(sequence.error().message);
    }
    const auto stitches = stitch::compute_stats(*sequence).stitches;
    if (requested != 0 && stitches != requested) {
        throw std::runtime_error("Synthetic scene does not contain the requested stitch count");
    }
    result["requested_stitches"] =
        requested == 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(double(requested));
    result["actual_stitches"] = double(stitches);
    result["commands"] = double(snapshot->commandCount);
    result["rendered_segments"] = double(snapshot->segments.size());
    result["scene_build_ms"] = sceneMs;
    result["cpu_scene_vertex_bytes"] = double(snapshot->vertices.size() * sizeof(StudioVertex));
    result["cpu_scene_segment_bytes"] = double(snapshot->segments.size() * sizeof(StudioSegment));
    std::vector<stitch_render::ThreadSegment> threads;
    threads.reserve(snapshot->segments.size());
    for (const auto& segment : snapshot->segments) {
        threads.push_back(segment.thread);
    }
    result["segments_hash"] =
        QString::number(stitch_render::hash_segments(threads), 16); // equality across backends

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        loop.quit();
    });
    timeout.start(std::max(30000, (options.frames + options.warmup) * 1000));
    std::unique_ptr<QQuickWindow> quick;
    std::unique_ptr<PaintedCanvas> widgets;
    QMetaObject::Connection swapConnection;
    studio::GpuCanvas* gpu = nullptr;
    std::atomic<bool> pending{false};
    int completed = 0;
    bool first = true;
    Time requestedAt;
    Time previous;
    Time installedAt;
    double firstFrameMs = 0;
    std::uint64_t firstBuildCount = 0;
    std::vector<double> intervals;
    std::vector<double> latencies;
    std::function<void()> requestFrame;
    const auto complete = [&](Time finished) {
        if (first) {
            first = false;
            firstFrameMs = ms(installedAt, finished);
            firstBuildCount = gpu ? gpu->geometryBuildCount() : 0;
        } else {
            ++completed;
            if (completed > options.warmup) {
                intervals.push_back(ms(previous, finished));
                latencies.push_back(ms(requestedAt, finished));
            }
        }
        previous = finished;
        if (completed >= options.frames + options.warmup) {
            loop.quit();
        } else {
            QTimer::singleShot(0, &loop, [&] { requestFrame(); });
        }
    };
    const auto cleanup = qScopeGuard([&] {
        QObject::disconnect(swapConnection);
        // Stop the render thread before captured state dies, including on error/timeout.
        if (quick) {
            quick->hide();
            quick.reset();
        }
        if (widgets) {
            widgets->painted = {};
            widgets->hide();
        }
    });
    if (options.backend == "quick") {
        quick = std::make_unique<QQuickWindow>();
        auto format = quick->format();
        format.setSamples(4);
        quick->setFormat(format);
        quick->setTitle("OpenStitch Studio — benchmark");
        quick->setColor(QColor("#222329"));
        quick->resize(1280, 800);
        gpu = new studio::GpuCanvas(quick->contentItem());
        gpu->setWidth(1280);
        gpu->setHeight(800);
        swapConnection = QObject::connect(
            quick.get(), &QQuickWindow::frameSwapped, &loop,
            [&] {
                if (pending.exchange(false)) {
                    const Time finished = Clock::now();
                    QMetaObject::invokeMethod(
                        &loop, [&, finished] { complete(finished); }, Qt::QueuedConnection);
                }
            },
            Qt::DirectConnection);
        requestFrame = [&] {
            requestedAt = Clock::now();
            pending.store(true);
            const double sign = (completed / 30) % 2 == 0 ? 1.0 : -1.0;
            gpu->panBy(sign * 2.0, sign * 0.5);
            gpu->zoomAt(completed % 2 == 0 ? 1.002 : 1.0 / 1.002, 640, 400);
            quick->update();
        };
        installedAt = Clock::now();
        pending.store(true);
        gpu->setScene(snapshot);
        gpu->fitDesign();
        quick->show();
        result["completion_signal"] = "QQuickWindow::frameSwapped (render-thread timestamp)";
        result["msaa_samples_requested"] = 4;
        result["preview_raster_ms"] = QJsonValue::Null;
        result["cached_image_bytes"] = QJsonValue::Null;
    } else {
        const auto rasterStart = Clock::now();
        const auto preview = render_preview(project, 1, 3.0, TaskContext{});
        if (!preview) {
            throw std::runtime_error(preview.error().message);
        }
        const QImage image = QImage::fromData(preview->png.data(), int(preview->png.size()), "PNG");
        if (image.isNull()) {
            throw std::runtime_error("Cannot decode Widgets preview");
        }
        result["preview_raster_ms"] = ms(rasterStart, Clock::now());
        result["cached_image_bytes"] = double(image.sizeInBytes());
        widgets = std::make_unique<PaintedCanvas>();
        widgets->setWindowTitle("OpenStitch V2 Widgets — benchmark");
        widgets->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        widgets->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        widgets->resize(1280, 800);
        widgets->painted = [&](Time finished) {
            if (pending.exchange(false)) {
                complete(finished);
            }
        };
        requestFrame = [&] {
            requestedAt = Clock::now();
            pending.store(true);
            const double sign = (completed / 30) % 2 == 0 ? 1.0 : -1.0;
            const QPointF centre = widgets->mapToScene(QPoint(640, 400));
            const double scale = widgets->zoom();
            widgets->zoomBy(completed % 2 == 0 ? 1.002 : 1.0 / 1.002);
            widgets->centreOnPx(centre - QPointF(sign * 2.0 / scale, sign * 0.5 / scale));
            widgets->viewport()->update();
        };
        installedAt = Clock::now();
        pending.store(true);
        widgets->setImage(image);
        widgets->show();
        widgets->fitAll();
        result["completion_signal"] = "PreviewCanvas::paintEvent returned (CPU paint only)";
    }
    loop.exec();
    if (timedOut || intervals.empty()) {
        throw std::runtime_error(
            "No completed native frames before timeout; do not use offscreen FPS");
    }
    if (quick && quick->rendererInterface()->graphicsApi() == QSGRendererInterface::Null) {
        throw std::runtime_error("The null RHI backend cannot measure a displayed canvas");
    }
    QScreen* actualScreen = quick ? quick->screen() : widgets->screen();
    result["window_screen"] = actualScreen ? actualScreen->name() : QString();
    result["window_screen_refresh_hz"] =
        actualScreen ? QJsonValue(actualScreen->refreshRate()) : QJsonValue(QJsonValue::Null);
    result["window_device_pixel_ratio"] =
        quick ? quick->effectiveDevicePixelRatio() : widgets->devicePixelRatioF();
    result["viewport_logical_px"] =
        quick ? QJsonArray{gpu->width(), gpu->height()}
              : QJsonArray{widgets->viewport()->width(), widgets->viewport()->height()};
    double elapsed = 0;
    for (double value : intervals) {
        elapsed += value;
    }
    result["sampled_frames"] = int(intervals.size());
    result["warmup_frames"] = options.warmup;
    result["completion_fps"] = double(intervals.size()) * 1000.0 / elapsed;
    result["frame_interval_ms"] = distribution(intervals);
    result["local_interaction_to_completion_ms"] = distribution(latencies);
    result["install_to_first_completion_ms"] = firstFrameMs;
    result["backend"] = gpu ? gpu->backendName() : QString("Widgets raster/QPainter");
    result["software_backend"] = quick ? QJsonValue(quick->rendererInterface()->graphicsApi() ==
                                                    QSGRendererInterface::Software)
                                       : QJsonValue(true);
    result["geometry_build_count"] =
        gpu ? QJsonValue(double(gpu->geometryBuildCount())) : QJsonValue(QJsonValue::Null);
    result["initial_geometry_build_count"] =
        gpu ? QJsonValue(double(firstBuildCount)) : QJsonValue(QJsonValue::Null);
    result["qsg_resource_creation_cpu_ms"] =
        gpu ? QJsonValue(gpu->resourceCreationMs()) : QJsonValue(QJsonValue::Null);
    result["process_working_set_mb"] = workingSetMb(false);
    result["process_peak_working_set_mb"] = workingSetMb(true);
    result["gpu_memory_bytes"] = QJsonValue::Null;
    result["gpu_execution_ms"] = QJsonValue::Null;
    std::fprintf(stderr, "%s / %s: %.2f completions/s, median %.3f ms, p95 %.3f ms\n",
                 qPrintable(options.backend), qPrintable(name), result["completion_fps"].toDouble(),
                 percentile(intervals, 0.5), percentile(intervals, 0.95));
    return result;
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    Options options;
    const auto args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args[i];
        if (arg == "--backend" && i + 1 < args.size()) {
            options.backend = args[++i];
        } else if (arg == "--json" && i + 1 < args.size()) {
            options.output = args[++i];
        } else if (arg == "--scene" && i + 1 < args.size()) {
            options.scene = args[++i];
        } else if (arg == "--frames" && i + 1 < args.size()) {
            options.frames = args[++i].toInt();
        } else if (arg == "--warmup" && i + 1 < args.size()) {
            options.warmup = args[++i].toInt();
        } else if (arg == "--generate-project" && i + 1 < args.size()) {
            options.generateProject = args[++i];
        } else if (arg == "--image" && i + 1 < args.size()) {
            options.image = args[++i];
        } else if (arg == "--height-mm" && i + 1 < args.size()) {
            options.heightMm = args[++i].toDouble();
        } else if (!arg.startsWith('-') && options.project.isEmpty()) {
            options.project = arg;
        } else {
            std::fprintf(stderr,
                         "Usage: openstitch-studio-bench [project.osp] --backend quick|widgets "
                         "[--scene all|1000|10000|50000|100000|real] [--frames 240] "
                         "[--warmup 30] [--json results.json]\n");
            return 2;
        }
    }
    if (!options.generateProject.isEmpty()) {
        if (options.image.isEmpty() || !std::isfinite(options.heightMm) || options.heightMm <= 0) {
            std::fprintf(stderr, "Generation needs --image fixture.png --height-mm 300\n");
            return 2;
        }
        DigitizeRequest request;
        request.image = options.image.toStdWString();
        request.target_height_mm = options.heightMm;
        auto digitized = run_digitize(request, TaskContext{});
        if (!digitized) {
            std::fprintf(stderr, "Digitization failed: %s\n", digitized.error().message.c_str());
            return 1;
        }
        auto saved =
            project_io::save_project(options.generateProject.toStdWString(), digitized->project);
        if (!saved) {
            std::fprintf(stderr, "Cannot save project: %s\n", saved.error().message.c_str());
            return 1;
        }
        std::fprintf(stdout,
                     "Generated %s: %zu objects, %zu stitches; stitched bounds %.3f x "
                     "%.3f mm (source height %.3f mm)\n",
                     qPrintable(options.generateProject),
                     digitized->project.embroidery_objects.size(), digitized->stats.stitches,
                     digitized->width_mm, digitized->height_mm, options.heightMm);
        return 0;
    }
    if ((options.backend != "quick" && options.backend != "widgets") || options.frames < 2 ||
        options.warmup < 0 || options.scene.isEmpty() ||
        (!QStringList{"all", "real", "1000", "10000", "50000", "100000"}.contains(options.scene))) {
        std::fprintf(stderr, "Invalid benchmark options\n");
        return 2;
    }
    if ((options.scene == "real" || options.scene == "all") && options.project.isEmpty()) {
        std::fprintf(stderr, "A real .osp is required for --scene real/all\n");
        return 2;
    }
    if (QGuiApplication::platformName() == "offscreen" ||
        QGuiApplication::platformName() == "minimal") {
        std::fprintf(stderr,
                     "Run on a native visible window; offscreen cadence is not screen FPS\n");
        return 2;
    }
    QJsonObject report{{"schema", 1},
                       {"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                       {"qt", qVersion()},
                       {"os", QSysInfo::prettyProductName()},
                       {"cpu_architecture", QSysInfo::currentCpuArchitecture()},
                       {"qpa_platform", QGuiApplication::platformName()},
                       {"viewport_logical_px", QJsonArray{1280, 800}},
                       {"project", options.project},
                       {"note",
                        "Quick swap cadence and Widgets CPU paint cadence are different completion "
                        "boundaries; neither measures physical input-to-photon latency."}};
#ifdef NDEBUG
    report["configuration"] = "Release";
#else
    report["configuration"] = "Debug";
#endif
    if (auto* screen = app.primaryScreen()) {
        report["screen"] = screen->name();
        report["screen_refresh_hz_reported"] = screen->refreshRate();
        report["device_pixel_ratio"] = screen->devicePixelRatio();
    }
    try {
        QJsonArray rows;
        for (std::size_t count : {1000U, 10000U, 50000U, 100000U}) {
            const auto name = QString::number(count);
            if (options.scene == "all" || options.scene == name) {
                const auto start = Clock::now();
                auto project = synthetic(count);
                const double loadMs = ms(start, Clock::now());
                rows.append(run(options, name, std::move(project), count, loadMs));
            }
        }
        if (options.scene == "all" || options.scene == "real") {
            const auto start = Clock::now();
            auto loaded = project_io::load_project(options.project.toStdWString());
            const double loadMs = ms(start, Clock::now());
            if (!loaded) {
                throw std::runtime_error(loaded.error().message);
            }
            rows.append(run(options, "real",
                            std::make_shared<const document::Project>(std::move(*loaded)), 0,
                            loadMs));
        }
        report["results"] = rows;
        const QByteArray json = QJsonDocument(report).toJson(QJsonDocument::Indented);
        if (options.output.isEmpty()) {
            std::fwrite(json.constData(), 1, std::size_t(json.size()), stdout);
        } else {
            QFile file(options.output);
            if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size()) {
                throw std::runtime_error("Cannot write benchmark JSON");
            }
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Benchmark failed: %s\n", error.what());
        return 1;
    }
    return 0;
}
