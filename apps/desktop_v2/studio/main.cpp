// SPDX-License-Identifier: Apache-2.0
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>

#include <QDir>
#include <QStandardPaths>

#include "action_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "studio_controller.hpp"

using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

int main(int argc, char** argv) {
    auto format = QSurfaceFormat::defaultFormat();
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("OpenStitch Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1 experimental"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Prototype Qt Quick / canevas GPU OpenStitch"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Projet .osp à ouvrir"));
    parser.addOption({QStringLiteral("software"), QStringLiteral("Rendu CPU Qt Quick explicite")});
    parser.addOption({QStringLiteral("screenshot"),
                      QStringLiteral("Capture réelle Qt, puis quitte"), QStringLiteral("png")});
    parser.addOption({QStringLiteral("select-object"),
                      QStringLiteral("Sélection initiale par identifiant (recette reproductible)"),
                      QStringLiteral("id")});
    parser.process(app);
    if (parser.isSet(QStringLiteral("software"))) {
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    }
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                 QStringLiteral("Provided by the application"));
    DocumentSession session;
    StudioController controller(session);
    ActionController actions(controller);
    FileController files(controller, &actions,
                         QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("studio"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("actions"), &actions);
    engine.rootContext()->setContextProperty(QStringLiteral("files"), &files);
    engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 2;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) {
        return 2;
    }
    if (parser.isSet(QStringLiteral("select-object"))) {
        const auto id = parser.value(QStringLiteral("select-object"));
        QObject::connect(&controller, &StudioController::loadFinished, &app, [&, id](bool success) {
            if (success) {
                controller.selectObject(id);
            }
        });
    }
    if (!parser.positionalArguments().isEmpty()) {
        files.openPath(QDir::current().absoluteFilePath(parser.positionalArguments().first()));
    }
    QTimer capture;
    bool capturePrimed = false;
    if (parser.isSet(QStringLiteral("screenshot"))) {
        const auto output = parser.value(QStringLiteral("screenshot"));
        QObject::connect(&controller, &StudioController::loadFinished, &app, [&app](bool success) {
            if (!success) {
                app.exit(3);
            }
        });
        capture.setInterval(50);
        QObject::connect(&capture, &QTimer::timeout, &app, [&, output] {
            auto* canvas = window->findChild<GpuCanvas*>(QStringLiteral("studioCanvas"));
            if (controller.busy()) {
                return;
            }
            const auto screenshot = window->grabWindow();
            if (screenshot.isNull()) {
                window->update();
                return;
            }
            // grabWindow also renders a hidden native window. Prime once so resource
            // diagnostics and the native DPR/viewport have synchronized before saving.
            if (!capturePrimed ||
                (controller.scene() && (!canvas || canvas->geometryBuildCount() == 0))) {
                capturePrimed = true;
                window->update();
                return;
            }
            qInfo("Studio capture: logical=%dx%d image=%dx%d DPR=%.2f backend=%s", window->width(),
                  window->height(), screenshot.width(), screenshot.height(),
                  window->effectiveDevicePixelRatio(),
                  canvas ? qPrintable(canvas->backendName()) : "none");
            app.exit(screenshot.save(output) ? 0 : 4);
        });
        capture.start();
        QTimer::singleShot(60'000, &app, [&app] { app.exit(5); });
    }
    return app.exec();
}
