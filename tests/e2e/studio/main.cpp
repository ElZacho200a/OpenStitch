// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"

#include <QQuickStyle>
#include <cstdlib>

using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

int main(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("OpenStitch E2E"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                 QStringLiteral("Provided by the application"));
    const auto arguments = app.arguments();
    if (arguments.size() == 4 && arguments[1] == QStringLiteral("--prepare-interrupted-session")) {
        Harness writer(arguments[2]);
        if (!writer.ready() || !writer.rectangles())
            return 80;
        writer.key(Qt::Key_A, Qt::ControlModifier);
        writer.key(Qt::Key_Right);
        if (!writer.settle() || !writer.files.autosaveNow())
            return 81;
        const auto saved = openstitch::project_io::save_project(
            std::filesystem::path(arguments[3].toStdWString()), writer.session.project());
        if (!saved)
            return 82;
        std::_Exit(86); // Simulated interruption: deliberately bypass every application destructor.
    }
    const auto selected = qgetenv("STUDIO_E2E_JOURNEY");
    int result = 0;
    bool ran = false;
    for (const auto& journey : journeys()) {
        if (!selected.isEmpty() && selected != journey.code) {
            continue;
        }
        ran = true;
        auto test = journey.create();
        result |= QTest::qExec(test.get(), argc, argv);
    }
    return ran ? result : 2;
}
