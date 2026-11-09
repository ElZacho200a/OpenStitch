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
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "point_edit_controller.hpp"
#include "points_space_controller.hpp"
#include "satin_controller.hpp"
#include "shape_batch.hpp"
#include "studio_controller.hpp"
#include "thread_controller.hpp"
#include "thread_plan.hpp"
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
bool mouseClick(QQuickWindow* window, const char* name) {
    auto* item = named(window, name);
    if (!item || !item->isEnabled() || !item->isVisible()) {
        return false;
    }
    const auto center =
        item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center);
    return true;
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

    void threadStripSelectsMovesAndRecolors() {
        Rig rig;
        open(rig);
        QVERIFY(rig.session.applyBatch(batchAddRect({60, 0}, 20, 20), "B").committed);
        QVERIFY(rig.session.applyBatch(batchAddRect({90, 0}, 20, 20), "C").committed);
        const auto& objects = rig.session.project().embroidery_objects;
        QCOMPARE(objects.size(), std::size_t{3});
        const auto first = objects[0].id;
        const auto second = objects[1].id;
        const auto third = objects[2].id;
        rig.session.execute(std::make_unique<commands::SetObjectThreadCommand>(
            std::vector<ObjectId>{second}, std::nullopt, std::array<std::uint8_t, 3>{255, 0, 0}));
        QTRY_VERIFY(named(rig.window, "pointsThreadSwatch1"));
        QVERIFY(mouseClick(rig.window, "pointsThreadSwatch0"));
        QCOMPARE(rig.session.selection().targets().size(), std::size_t{2});
        QVERIFY(rig.session.selection().contains({TargetKind::EmbroideryObject, first.value}));
        QVERIFY(rig.session.selection().contains({TargetKind::EmbroideryObject, third.value}));

        const auto center = [&](const char* name) {
            const auto* item = named(rig.window, name);
            return item->mapToScene({item->width() / 2, item->height() / 2}).toPoint();
        };
        const auto start = center("pointsThreadSwatch0");
        const auto end = center("pointsThreadSwatch1");
        const auto beforeMove = rig.session.historyPosition();
        QTest::mousePress(rig.window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(rig.window, end);
        QCOMPARE(rig.session.historyPosition(), beforeMove);
        QTest::mouseRelease(rig.window, Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(rig.session.historyPosition(), beforeMove + 1);
        const auto moved = orderRows(rig.session.project());
        QCOMPARE(moved[0].id, second.value);
        QCOMPARE(moved[1].id, first.value);
        QCOMPARE(moved[2].id, third.value);
        QVERIFY(rig.session.undo());
        QCOMPARE(orderRows(rig.session.project())[0].id, first.value);
        QVERIFY(rig.session.redo());
        QCOMPARE(orderRows(rig.session.project())[0].id, second.value);

        QTRY_VERIFY(named(rig.window, "pointsThreadSwatch1"));
        auto* dialog = rig.window->findChild<QObject*>(QStringLiteral("pointsThreadColorDialog"));
        QVERIFY(dialog);
        QTest::mouseClick(rig.window, Qt::RightButton, Qt::NoModifier,
                          center("pointsThreadSwatch1"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        const QColor blue(0, 0, 255);
        QVERIFY(dialog->setProperty("selectedColor", blue));
        const auto beforeColor = rig.session.historyPosition();
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QCOMPARE(rig.session.historyPosition(), beforeColor + 1);
        QCOMPARE(rig.threads.entries()[1].toMap().value(QStringLiteral("color")).value<QColor>(),
                 blue);
        QCOMPARE(threadEntries(rig.session.project())[1].objects.size(), std::size_t{2});
        QVERIFY(rig.session.undo());
        QVERIFY(rig.session.redo());

        const auto saved = rig.cfg / "thread-strip.osp";
        QVERIFY(project_io::save_project(saved, rig.session.project()).has_value());
        const auto reopened = project_io::load_project(saved);
        QVERIFY(reopened.has_value());
        QCOMPARE(orderRows(*reopened)[0].id, second.value);
        QCOMPARE(orderRows(*reopened)[1].id, first.value);
        QCOMPARE(threadEntries(*reopened)[1].label, std::string("#0000ff"));
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void panelToolbarButtonsReceiveRealMouseClicks() {
        Rig rig;
        open(rig);
        auto* digitizeHub = named(rig.window, "digitizeHub");
        auto* viewHub = named(rig.window, "viewHub");
        QVERIFY(digitizeHub && viewHub);
        QTRY_VERIFY(digitizeHub->property("ready").toBool());
        QTRY_VERIFY(viewHub->property("ready").toBool());

        // Previous tests called AbstractButton.click() directly. That cannot reveal
        // input occlusion from an overlapping overlay, popup or sibling item.
        QVERIFY(mouseClick(rig.window, "satinPanelButton"));
        QTRY_COMPARE(digitizeHub->property("openPanel").toString(), QStringLiteral("satin"));
        QVERIFY(mouseClick(rig.window, "viewPanelButton"));
        QTRY_COMPARE(viewHub->property("openPanel").toString(), QStringLiteral("view"));
        QCOMPARE(digitizeHub->property("openPanel").toString(), QString());
        QVERIFY(mouseClick(rig.window, "fadePanelButton"));
        QTRY_COMPARE(digitizeHub->property("openPanel").toString(), QStringLiteral("fade"));
        QCOMPARE(viewHub->property("openPanel").toString(), QString());
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
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

    void narrowObjectPanelKeepsTreeEditActionsAndInspectorSeparated() {
        Rig rig;
        open(rig);
        rig.window->resize(1000, 640);
        rig.studio.selectObject(
            QString::number(rig.session.project().embroidery_objects.front().id.value));
        rig.workspace->setEditOpen(true);
        QTRY_VERIFY_WITH_TIMEOUT(rig.inspector.count() > 0, 5'000);

        auto* panel = named(rig.window, "pointsObjectsPanel");
        auto* scroll = named(rig.window, "pointsEditActionsScroll");
        auto* inspectorArea = named(rig.window, "inspectorArea");
        auto* selectionFlow = named(rig.window, "editSelectionFlow");
        auto* transformFlow = named(rig.window, "editTransformFlow");
        auto* placementFlow = named(rig.window, "editPlacementFlow");
        QVERIFY(panel && scroll && inspectorArea);
        QVERIFY(selectionFlow && transformFlow && placementFlow);
        const auto top = [](QQuickItem* item) { return item->mapToScene(QPointF(0.0, 0.0)).y(); };
        const auto bottom = [&](QQuickItem* item) { return top(item) + item->height(); };

        for (const bool treeMode : {true, false}) {
            rig.workspace->setTreeMode(treeMode);
            auto* list = named(rig.window, treeMode ? "objectTreeList" : "objectList");
            QVERIFY(list);
            QTRY_VERIFY_WITH_TIMEOUT(scroll->height() >= 24 && list->height() >= 24, 5'000);
            QVERIFY(scroll->isVisible());
            QVERIFY(inspectorArea->isVisible());
            // Every wrapped row contributes to EditBar's measured height.
            for (auto* flow : {selectionFlow, transformFlow, placementFlow}) {
                QVERIFY2(flow->height() + 1.0 >= flow->childrenRect().height(),
                         qPrintable(flow->objectName()));
            }
            if (treeMode) {
                auto* layerActions = named(rig.window, "layerActionsFlow");
                QVERIFY(layerActions);
                QTRY_VERIFY(layerActions->height() >= layerActions->childrenRect().height() - 1.0);
                QVERIFY(bottom(layerActions) <= top(list) + 2.0);
            }
            // On screen, the three panels must be disjoint, not painted on top
            // of each other. The edit actions use their own clipped Flickable.
            QTRY_VERIFY(bottom(list) <= top(scroll) + 2.0);
            QTRY_VERIFY(bottom(scroll) <= bottom(panel) + 2.0);
            QTRY_VERIFY(bottom(panel) <= top(inspectorArea) + 2.0);
            QVERIFY(scroll->property("contentHeight").toDouble() >= scroll->height());
            QVERIFY(top(inspectorArea) < bottom(inspectorArea));
        }

        // The last actions remain accessible by scrolling, not by overflowing.
        const double maximum = scroll->property("contentHeight").toDouble() - scroll->height();
        QVERIFY(maximum > 5.0);
        QVERIFY(scroll->setProperty("contentY", maximum));
        QTRY_VERIFY(scroll->property("contentY").toDouble() > 0.0);
        QVERIFY(named(rig.window, "deleteButton"));
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
        QVERIFY2(ids.contains(QStringLiteral("auto_satin")), qPrintable(ids.join(',')));
        QVERIFY2(ids.contains(QStringLiteral("tatami")), qPrintable(ids.join(',')));
        auto* box = named(rig.window, "typeBox");
        QVERIFY(box);
        QVERIFY(box->isVisible());
        QCOMPARE(box->property("count").toInt(), static_cast<int>(types.size()));
        // Chaque type du moteur est atteignable : le sélecteur le convertit en une étape.
        const auto depth = rig.session.undoNames().size();
        QString other;
        for (const auto& t : types) {
            if (!t.toMap().value(QStringLiteral("current")).toBool()) {
                other = t.toMap().value(QStringLiteral("id")).toString();
                break;
            }
        }
        QVERIFY(!other.isEmpty());
        rig.inspector.setStitchType(other);
        QTRY_VERIFY(rig.session.undoNames().size() > depth);
    }

    void railHoldsPointToolsOnly() {
        Rig rig;
        open(rig);
        QVERIFY(named(rig.window, "pointsToolRail"));
        for (const char* name :
             {"gesture_satin", "gesture_guide", "gesture_break", "gesture_fade", "gesture_points",
              "gesture_rungs", "selectToolButton", "panToolButton", "fitToolButton"}) {
            QVERIFY2(named(rig.window, name), name);
        }
        // Les outils de dessin vectoriel quittent cet espace.
        for (const char* name : {"tool_draw.rectangle", "tool_draw.ellipse", "tool_draw.bezier",
                                 "tool_node.edit", "tool_knife"}) {
            QVERIFY2(!named(rig.window, name), name);
        }
        QVERIFY(press(rig.window, "gesture_guide"));
        QCOMPARE(rig.digitizer.mode(), QStringLiteral("guide"));
        QVERIFY(press(rig.window, "selectToolButton"));
        QCOMPARE(rig.digitizer.mode(), QString());
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
        QTRY_VERIFY(rig.window->findChild<GpuCanvas*>(QStringLiteral("studioCanvas")) == canvas);
        QVERIFY(!rig.pointsSpace.camera().isEmpty());
        QCOMPARE(rig.pointsSpace.camera().value(QStringLiteral("zoom")).toDouble() > 0.0, true);
        QVERIFY(kept.contains(QStringLiteral("zoom")));
    }
};

QTEST_MAIN(StudioSpacePointsTest)
#include "test_studio_space_points.moc"
