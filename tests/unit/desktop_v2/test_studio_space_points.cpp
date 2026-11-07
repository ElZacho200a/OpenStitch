// SPDX-License-Identifier: Apache-2.0
// Espace « Points » : mise en page (un seul panneau latéral, barre d'options contextuelle, plus de
// boutons flottants), sélecteur de type, rail d'outils de points, rendu réaliste, état publié,
// lien vers la forme source et mémoire de caméra. Vrai Main.qml, vrai moteur.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <filesystem>

#include "action_controller.hpp"
#include "analysis_controller.hpp"
#include "digitize_gestures.hpp"
#include "edit_controller.hpp"
#include "fade_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "inspector_controller.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "point_edit_controller.hpp"
#include "points_space_controller.hpp"
#include "satin_controller.hpp"
#include "shape_batch.hpp"
#include "studio_controller.hpp"
#include "thread_controller.hpp"
#include "tool_controller.hpp"
#include "view_controller.hpp"
#include "workspace_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

QQuickItem* findVisual(QQuickItem* item, const QString& name) {
    if (item->objectName() == name) {
        return item;
    }
    for (auto* child : item->childItems()) {
        if (auto* found = findVisual(child, name)) {
            return found;
        }
    }
    return nullptr;
}
QQuickItem* named(QQuickWindow* window, const char* name) {
    return findVisual(window->contentItem(), QString::fromLatin1(name));
}
bool press(QQuickWindow* window, const char* name) {
    auto* item = named(window, name);
    return item && item->isEnabled() && QMetaObject::invokeMethod(item, "click");
}

struct Rig {
    DocumentSession session;
    StudioController studio{session};
    ActionController actions{studio};
    EditController editor{studio};
    QTemporaryDir dir;
    std::filesystem::path cfg{dir.path().toStdWString()};
    FileController files{studio, &actions, dir.path()};
    InspectorController inspector{studio};
    ViewController view{studio, cfg};
    ThreadController threads{studio, cfg};
    AnalysisController analysis{studio, view, cfg};
    SatinController satin{studio};
    FadeController fade{studio};
    PointEditController pointEdit{studio};
    ToolController tools{studio};
    DigitizeGestures digitizer{studio, pointEdit, fade, satin};
    PointsSpaceController pointsSpace{studio};
    QQmlApplicationEngine engine;
    QStringList warnings;
    QQuickWindow* window{nullptr};
    WorkspaceController* workspace{nullptr};
};

} // namespace

class StudioSpacePointsTest : public QObject {
    Q_OBJECT
private:
    void open(Rig& rig) {
        connect(&rig.engine, &QQmlApplicationEngine::warnings, this,
                [&rig](const QList<QQmlError>& errors) {
                    for (const auto& e : errors) {
                        rig.warnings.push_back(e.toString());
                    }
                });
        auto* ctx = rig.engine.rootContext();
        ctx->setContextProperty(QStringLiteral("studio"), &rig.studio);
        ctx->setContextProperty(QStringLiteral("actions"), &rig.actions);
        ctx->setContextProperty(QStringLiteral("editor"), &rig.editor);
        ctx->setContextProperty(QStringLiteral("files"), &rig.files);
        ctx->setContextProperty(QStringLiteral("inspector"), &rig.inspector);
        ctx->setContextProperty(QStringLiteral("view"), &rig.view);
        ctx->setContextProperty(QStringLiteral("threads"), &rig.threads);
        ctx->setContextProperty(QStringLiteral("analysis"), &rig.analysis);
        ctx->setContextProperty(QStringLiteral("satin"), &rig.satin);
        ctx->setContextProperty(QStringLiteral("fade"), &rig.fade);
        ctx->setContextProperty(QStringLiteral("pointEdit"), &rig.pointEdit);
        ctx->setContextProperty(QStringLiteral("tools"), &rig.tools);
        ctx->setContextProperty(QStringLiteral("digitizer"), &rig.digitizer);
        ctx->setContextProperty(QStringLiteral("pointsSpace"), &rig.pointsSpace);
        rig.engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        QVERIFY2(!rig.engine.rootObjects().isEmpty(), qPrintable(rig.warnings.join('\n')));
        rig.window = qobject_cast<QQuickWindow*>(rig.engine.rootObjects().front());
        QVERIFY(rig.window);
        rig.workspace = rig.window->findChild<WorkspaceController*>();
        QVERIFY(rig.workspace);
        auto project = project_ops::create_empty_project({});
        QVERIFY(project.has_value());
        rig.session.replaceProject(std::move(*project), true);
        QVERIFY(rig.session.applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle").committed);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.studio.busy() && rig.studio.scene() != nullptr, 15'000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.window->isExposed(), 5'000);
        QCOMPARE(rig.workspace->current(), QStringLiteral("points"));
    }

private slots:
    void initTestCase() {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
        qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                     QStringLiteral("Provided by the application"));
    }

    void noFloatingToolbarsAndOneSidePanel() {
        Rig rig;
        open(rig);
        auto* digitizeHub = named(rig.window, "digitizeHub");
        auto* viewHub = named(rig.window, "viewHub");
        QVERIFY(digitizeHub && viewHub);
        QTRY_VERIFY(digitizeHub->property("ready").toBool());
        QTRY_VERIFY(viewHub->property("ready").toBool());
        // Plus de barres flottantes sur le canevas : tout vit dans la barre de panneaux et la barre
        // d'options.
        QVERIFY(!named(rig.window, "digitizeToolbar"));
        QVERIFY(!named(rig.window, "gestureToolbar"));
        QVERIFY(!named(rig.window, "viewToolbar"));
        QVERIFY(named(rig.window, "pointsPanelBar"));
        QVERIFY(named(rig.window, "pointsOptionsBar"));

        QVERIFY(press(rig.window, "satinPanelButton"));
        QCOMPARE(digitizeHub->property("openPanel").toString(), QStringLiteral("satin"));
        QVERIFY(press(rig.window, "viewPanelButton"));
        QCOMPARE(viewHub->property("openPanel").toString(), QStringLiteral("view"));
        QCOMPARE(digitizeHub->property("openPanel").toString(), QString());
        QVERIFY(press(rig.window, "fadePanelButton"));
        QCOMPARE(digitizeHub->property("openPanel").toString(), QStringLiteral("fade"));
        QCOMPARE(viewHub->property("openPanel").toString(), QString());
        // Même par la propriété (--open-panels) : jamais deux panneaux ensemble.
        viewHub->setProperty("openPanel", QStringLiteral("analysis"));
        QCOMPARE(digitizeHub->property("openPanel").toString(), QString());
        QCOMPARE(viewHub->property("openPanel").toString(), QStringLiteral("analysis"));
        // Un second clic referme.
        QVERIFY(press(rig.window, "analysisPanelButton"));
        QCOMPARE(viewHub->property("openPanel").toString(), QString());
        for (const char* name :
             {"pullPanelButton", "appliquePanelButton", "pointEditPanelButton"}) {
            if (named(rig.window, name)) {
                QVERIFY(press(rig.window, name));
                const bool one = digitizeHub->property("openPanel").toString().isEmpty() !=
                                 viewHub->property("openPanel").toString().isEmpty();
                QVERIFY2(one, name);
            }
        }
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void everyPointTypeIsInTheSelector() {
        Rig rig;
        open(rig);
        rig.studio.selectObject(
            QString::number(rig.session.project().embroidery_objects.front().id.value));
        QTRY_VERIFY(rig.inspector.count() > 0);
        const auto types = rig.inspector.types();
        QVERIFY(types.size() >= 6);
        QStringList ids;
        for (const auto& t : types) {
            ids << t.toMap().value(QStringLiteral("id")).toString();
        }
        QVERIFY2(ids.contains(QStringLiteral("satin")), qPrintable(ids.join(',')));
        QVERIFY2(ids.contains(QStringLiteral("tatami")), qPrintable(ids.join(',')));
        auto* box = named(rig.window, "typeBox");
        QVERIFY(box);
        QVERIFY(box->isVisible());
        QCOMPARE(box->property("count").toInt(), static_cast<int>(types.size()));
        // Chaque type du moteur est atteignable : le sélecteur le convertit en une étape.
        const auto depth = rig.session.undoNames().size();
        rig.inspector.setStitchType(QStringLiteral("satin"));
        QTRY_VERIFY(rig.session.undoNames().size() > depth);
    }

    void railHoldsPointToolsOnly() {
        Rig rig;
        open(rig);
        QVERIFY(named(rig.window, "pointsToolRail"));
        for (const char* name :
             {"tool_satin.column", "tool_fill.direction-guide", "tool_fill.break-line",
              "tool_fade.axis", "tool_stitch.points", "tool_text", "selectToolButton",
              "panToolButton", "fitToolButton"}) {
            QVERIFY2(named(rig.window, name), name);
        }
        // Les outils de dessin vectoriel quittent cet espace.
        for (const char* name : {"tool_draw.rectangle", "tool_draw.ellipse", "tool_draw.bezier",
                                 "tool_node.edit", "tool_knife"}) {
            QVERIFY2(!named(rig.window, name), name);
        }
        QVERIFY(press(rig.window, "tool_satin.column"));
        QCOMPARE(rig.tools.activeTool(), QStringLiteral("satin.column"));
        QVERIFY(press(rig.window, "tool_satin.column"));
        QCOMPARE(rig.tools.activeTool(), QStringLiteral("select"));
    }

    void gesturesLiveInTheOptionsBar() {
        Rig rig;
        open(rig);
        for (const char* name : {"gesture_points", "gesture_fade", "gesture_satin", "gesture_guide",
                                 "gesture_break", "gesture_rungs"}) {
            QVERIFY2(named(rig.window, name), name);
        }
        auto* canvas = rig.window->findChild<GpuCanvas*>();
        QVERIFY(canvas);
        QVERIFY(canvas->property("digitize").value<QObject*>() == &rig.digitizer);
        QVERIFY(press(rig.window, "gesture_satin"));
        QCOMPARE(rig.digitizer.mode(), QStringLiteral("satin"));
        QVERIFY(press(rig.window, "gesture_satin"));
        QCOMPARE(rig.digitizer.mode(), QString());
    }

    void realisticRenderingToggles() {
        Rig rig;
        open(rig);
        const bool before = rig.view.realistic();
        QVERIFY(press(rig.window, "realisticButton2"));
        QCOMPARE(rig.view.realistic(), !before);
        QVERIFY(press(rig.window, "realisticButton2"));
        QCOMPARE(rig.view.realistic(), before);
    }

    void publishesTheStageAndLinksTheSource() {
        Rig rig;
        open(rig);
        QTRY_COMPARE(rig.workspace->space(QStringLiteral("points")).value("state").toString(),
                     QStringLiteral("upToDate"));
        QVERIFY(rig.workspace->space(QStringLiteral("points"))
                    .value("detail")
                    .toString()
                    .contains(QStringLiteral("Points à jour")));
        QCOMPARE(rig.pointsSpace.orphanCount(), 0);
        const auto objectId =
            QString::number(rig.session.project().embroidery_objects.front().id.value);
        QVERIFY(rig.pointsSpace.hasSource(objectId));
        auto* subtitle = named(rig.window, "objectSubtitle");
        QVERIFY(subtitle);
        QVERIFY2(subtitle->property("text").toString().contains(QStringLiteral("forme")),
                 qPrintable(subtitle->property("text").toString()));

        // La forme source disparaît : l'objet orphelin est signalé, l'état passe à « obsolète ».
        auto project = rig.session.project();
        project.vector_objects.clear();
        rig.session.replaceProject(std::move(project), false);
        QTRY_COMPARE(rig.pointsSpace.orphanCount(), 1);
        QTRY_COMPARE(rig.workspace->space(QStringLiteral("points")).value("state").toString(),
                     QStringLiteral("stale"));
        QTRY_VERIFY(rig.workspace->space(QStringLiteral("points"))
                        .value("detail")
                        .toString()
                        .contains(QStringLiteral("forme source")));
        QVERIFY(!rig.pointsSpace.hasSource(objectId));
    }

    void keepsTheCameraOfTheSpace() {
        Rig rig;
        open(rig);
        auto* canvas = rig.window->findChild<GpuCanvas*>();
        QVERIFY(canvas);
        QVERIFY(rig.pointsSpace.camera().isEmpty());
        rig.workspace->setCurrent(QStringLiteral("image"));
        QTRY_VERIFY(!rig.pointsSpace.camera().isEmpty());
        const auto kept = rig.pointsSpace.camera();
        rig.workspace->setCurrent(QStringLiteral("points"));
        QTRY_COMPARE(rig.window->findChildren<GpuCanvas*>().size(), 1);
        QVERIFY(!rig.pointsSpace.camera().isEmpty());
        QCOMPARE(rig.pointsSpace.camera().value(QStringLiteral("zoom")).toDouble() > 0.0, true);
        QVERIFY(kept.contains(QStringLiteral("zoom")));
    }
};

QTEST_MAIN(StudioSpacePointsTest)
#include "test_studio_space_points.moc"
