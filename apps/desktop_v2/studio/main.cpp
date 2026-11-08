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
#include <QFileInfo>
#include <QStandardPaths>
#include <filesystem>

#include "action_controller.hpp"
#include "analysis_controller.hpp"
#include "applique_controller.hpp"
#include "companion_controller.hpp"
#include "digitize_gestures.hpp"
#include "edit_controller.hpp"
#include "fade_controller.hpp"
#include "file_controller.hpp"
#include "fix_controller.hpp"
#include "gpu_canvas.hpp"
#include "help_controller.hpp"
#include "history_controller.hpp"
#include "image_controller.hpp"
#include "image_space_controller.hpp"
#include "inspector_controller.hpp"
#include "point_edit_controller.hpp"
#include "points_space_controller.hpp"
#include "preferences_controller.hpp"
#include "pull_controller.hpp"
#include "region_controller.hpp"
#include "satin_controller.hpp"
#include "segmentation_space_controller.hpp"
#include "studio_controller.hpp"
#include "thread_controller.hpp"
#include "tool_controller.hpp"
#include "vector_controller.hpp"
#include "vector_space_controller.hpp"
#include "view_controller.hpp"
#include "workspace_controller.hpp"

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
    parser.addOption(
        {QStringLiteral("open-space"),
         QStringLiteral("Recette : ouvre cet espace (image, segmentation, vector, points)"),
         QStringLiteral("espace")});
    parser.addOption({QStringLiteral("select-object"),
                      QStringLiteral("Sélection initiale par identifiant (recette reproductible)"),
                      QStringLiteral("id")});
    parser.addOption(
        {QStringLiteral("open-panels"),
         QStringLiteral(
             "Recette : ouvre ces panneaux avant --screenshot (liste séparée par des "
             "virgules : vue, fils, analyse, satin, fondu, points, tirage, applique, corriger, "
             "inspecteur ; un seul panneau à la fois par groupe, le dernier gagne)"),
         QStringLiteral("liste")});
    parser.process(app);
    {
        // Default font of every Qt control (Theme.fontFamily / Theme.bodySize): a control that is
        // not a Studio* component must still use the Studio type face, never the Qt default.
        QFont base(
#ifdef Q_OS_WIN
            QStringLiteral("Segoe UI Variable")
#else
            QStringLiteral("sans-serif")
#endif
        );
        base.setPixelSize(12);
        QGuiApplication::setFont(base);
    }
    if (parser.isSet(QStringLiteral("software"))) {
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    }
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                 QStringLiteral("Provided by the application"));
    DocumentSession session;
    StudioController controller(session);
    HistoryController history(controller);
    ActionController actions(controller);
    EditController editor(controller);
    FileController files(controller, &actions,
                         QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    InspectorController inspector(controller);
    ToolController tools(controller);
    VectorController vectors(controller);
    const std::filesystem::path configDir(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdWString());
    ViewController view(controller, configDir);
    ThreadController threads(controller, configDir);
    AnalysisController analysis(controller, view, configDir);
    ImageController imageTools(controller);
    RegionController regionTools(controller, imageTools);
    ImageSpaceController imageSpace(controller, imageTools);
    SegmentationSpaceController segmentationSpace(controller, imageTools, regionTools);
    SatinController satin(controller);
    FadeController fade(controller);
    PointEditController pointEdit(controller);
    PreferencesController prefs(configDir);
    HelpController help;
    CompanionController companion(controller);
    DigitizeGestures digitizer(controller, pointEdit, fade, satin);
    PullController pull(controller, &inspector);
    AppliqueController applique(controller, inspector);
    FixController fix(controller);
    PointsSpaceController pointsSpace(controller);
    VectorSpaceController vectorStage(controller);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("pointsSpace"), &pointsSpace);
    engine.rootContext()->setContextProperty(QStringLiteral("digitizer"), &digitizer);
    engine.rootContext()->setContextProperty(QStringLiteral("pull"), &pull);
    engine.rootContext()->setContextProperty(QStringLiteral("applique"), &applique);
    engine.rootContext()->setContextProperty(QStringLiteral("fix"), &fix);
    engine.addImageProvider(QStringLiteral("prepare"), imageTools.createProvider());
    engine.rootContext()->setContextProperty(QStringLiteral("satin"), &satin);
    engine.rootContext()->setContextProperty(QStringLiteral("fade"), &fade);
    engine.rootContext()->setContextProperty(QStringLiteral("prefs"), &prefs);
    engine.rootContext()->setContextProperty(QStringLiteral("help"), &help);
    engine.rootContext()->setContextProperty(QStringLiteral("companion"), &companion);
    engine.rootContext()->setContextProperty(QStringLiteral("pointEdit"), &pointEdit);
    engine.rootContext()->setContextProperty(QStringLiteral("editor"), &editor);
    engine.rootContext()->setContextProperty(QStringLiteral("studio"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("history"), &history);
    engine.rootContext()->setContextProperty(QStringLiteral("actions"), &actions);
    engine.rootContext()->setContextProperty(QStringLiteral("files"), &files);
    engine.rootContext()->setContextProperty(QStringLiteral("inspector"), &inspector);
    engine.rootContext()->setContextProperty(QStringLiteral("tools"), &tools);
    engine.rootContext()->setContextProperty(QStringLiteral("vectors"), &vectors);
    engine.rootContext()->setContextProperty(QStringLiteral("vectorStage"), &vectorStage);
    engine.rootContext()->setContextProperty(QStringLiteral("view"), &view);
    engine.rootContext()->setContextProperty(QStringLiteral("threads"), &threads);
    engine.rootContext()->setContextProperty(QStringLiteral("analysis"), &analysis);
    engine.rootContext()->setContextProperty(QStringLiteral("imageTools"), &imageTools);
    engine.rootContext()->setContextProperty(QStringLiteral("imageSpace"), &imageSpace);
    engine.rootContext()->setContextProperty(QStringLiteral("regionTools"), &regionTools);
    engine.rootContext()->setContextProperty(QStringLiteral("segmentationSpace"),
                                             &segmentationSpace);
    engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 2;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) {
        return 2;
    }
    if (parser.isSet(QStringLiteral("open-panels"))) {
        const auto names =
            parser.value(QStringLiteral("open-panels")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        auto* viewHub = window->findChild<QObject*>(QStringLiteral("viewHub"));
        auto* digitizeHub = window->findChild<QObject*>(QStringLiteral("digitizeHub"));
        for (const auto& raw : names) {
            const auto name = raw.trimmed().toLower();
            // Ouverture directe d'un espace (recette / capture) : « espace-segmentation », ...
            if (name.startsWith(QStringLiteral("espace-"))) {
                if (auto* workspace = window->findChild<WorkspaceController*>()) {
                    workspace->setCurrent(name.mid(7));
                    viewHub = window->findChild<QObject*>(QStringLiteral("viewHub"));
                    digitizeHub = window->findChild<QObject*>(QStringLiteral("digitizeHub"));
                }
            } else if (name == QStringLiteral("vue") && viewHub) {
                viewHub->setProperty("openPanel", QStringLiteral("view"));
            } else if (name == QStringLiteral("fils") && viewHub) {
                viewHub->setProperty("openPanel", QStringLiteral("threads"));
            } else if (name == QStringLiteral("analyse") && viewHub) {
                viewHub->setProperty("openPanel", QStringLiteral("analysis"));
            } else if (digitizeHub &&
                       (name == QStringLiteral("satin") || name == QStringLiteral("points") ||
                        name == QStringLiteral("applique"))) {
                digitizeHub->setProperty("openPanel", name);
            } else if (digitizeHub && name == QStringLiteral("corriger")) {
                digitizeHub->setProperty("openPanel", QStringLiteral("fix"));
            } else if (digitizeHub && name == QStringLiteral("fondu")) {
                digitizeHub->setProperty("openPanel", QStringLiteral("fade"));
            } else if (digitizeHub && name == QStringLiteral("tirage")) {
                digitizeHub->setProperty("openPanel", QStringLiteral("pull"));
            } else if (name == QStringLiteral("espace-image")) {
                for (auto* child : window->findChildren<QObject*>()) {
                    if (QByteArray(child->metaObject()->className())
                            .contains("WorkspaceController")) {
                        child->setProperty("current", QStringLiteral("image"));
                        break;
                    }
                }
            } else if (name == QStringLiteral("inspecteur")) {
                window->setProperty("editOpen", true);
            }
        }
    }
    if (parser.isSet(QStringLiteral("open-space"))) {
        if (auto* workspace = window->findChild<WorkspaceController*>()) {
            workspace->setCurrent(parser.value(QStringLiteral("open-space")));
        }
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
    } else if (!parser.isSet(QStringLiteral("screenshot"))) {
        // Session restore: reopen the file left open at the last quit, if it still exists.
        const auto last = prefs.session().value(QStringLiteral("file")).toString();
        if (!last.isEmpty() && QFileInfo::exists(last)) {
            files.openPath(last);
        }
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
            if (!canvas) {
                canvas = window->findChild<GpuCanvas*>(); // canevas de l'espace courant
            }
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
            // The stitch-geometry wait only applies when this canvas draws stitches: the Image,
            // Segmentation and Vector spaces never build it, which used to hang the capture.
            const bool waitsForStitches =
                canvas && canvas->layerFlags().stitches && canvas->geometryBuildCount() == 0;
            if (!capturePrimed || (controller.scene() && waitsForStitches)) {
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
