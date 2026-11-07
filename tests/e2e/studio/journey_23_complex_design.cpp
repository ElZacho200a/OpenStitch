// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QAccessible>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSet>
#include <algorithm>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey23 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.tentabrode());
        const auto path = std::filesystem::path(
            h.directory.filePath(QStringLiteral("tentabrode-300mm.osp")).toStdWString());
        QVERIFY(project_io::save_project(path, h.session.project()).has_value());
        QElapsedTimer timer;
        timer.start();
        auto loaded = project_io::load_project(path);
        const double loadMs = timer.nsecsElapsed() / 1e6;
        QVERIFY(loaded.has_value());
        timer.restart();
        auto scene = build_studio_scene(std::make_shared<const document::Project>(*loaded), 1,
                                        TaskContext{});
        const double sceneMs = timer.nsecsElapsed() / 1e6;
        QVERIFY(scene.has_value());
        QVERIFY(!(*scene)->segments.empty());
        const auto before = h.bytes();
        std::vector<double> captureCosts;
        for (int index = 0; index < 10; ++index) {
            timer.restart();
            QVERIFY(!h.window->grabWindow().isNull());
            captureCosts.push_back(timer.nsecsElapsed() / 1e6);
        }
        std::sort(captureCosts.begin(), captureCosts.end());
        qInfo().noquote() << "E2E23 load_project_wall_ms=" << loadMs
                          << "build_studio_scene_wall_ms=" << sceneMs
                          << "capture_frame_readback_wall_p50_ms=" << captureCosts[5]
                          << "resource_creation_ms=" << h.canvas->resourceCreationMs()
                          << "backend=" << h.canvas->backendName()
#ifdef NDEBUG
                          << "configuration=Release";
#else
                          << "configuration=Debug";
#endif
        h.studio.selectObject(
            QString::number(h.session.project().embroidery_objects.front().id.value));
        h.key(Qt::Key_Right);
        QVERIFY(h.settle(60000));
        QVERIFY(h.undoToBytes(before));
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey23>("23");
}
#include "journey_23_complex_design.moc"
