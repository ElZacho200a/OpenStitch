// SPDX-License-Identifier: Apache-2.0
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <array>
#include <filesystem>

#include "action_controller.hpp"
#include "analysis_controller.hpp"
#include "edit_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "inspector_controller.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_render/segments.hpp"
#include "studio_controller.hpp"
#include "thread_controller.hpp"
#include "view_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {
constexpr std::uint64_t firstId = (1ULL << 54) + 17;
constexpr std::uint64_t secondId = (1ULL << 54) + 19;

document::Project fixture() {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    for (const auto id : {firstId, secondId}) {
        const std::int32_t y = id == firstId ? 10'000 : -10'000;
        document::VectorObject vector;
        vector.id = ObjectId{id - 1};
        geometry::Path path;
        path.closed = false;
        path.nodes = {{{Micrometers{-15'000}, Micrometers{y}}},
                      {{Micrometers{15'000}, Micrometers{y}}}};
        vector.paths.push_back({path, {}});
        project.vector_objects.push_back(vector);
        document::EmbroideryObject object;
        object.id = ObjectId{id};
        object.source_vector = vector.id;
        object.name = id == firstId ? "Raspberry thread" : "Mint thread";
        object.rgb = id == firstId ? std::array<std::uint8_t, 3>{180, 17, 65}
                                   : std::array<std::uint8_t, 3>{21, 180, 151};
        object.params = document::RunningStitchParams{Micrometers{2'000}};
        project.embroidery_objects.push_back(object);
    }
    project.object_ids.reset(secondId);
    return project;
}

struct Rig {
    DocumentSession session;
    StudioController studio{session};
    QTemporaryDir dir;
    ViewController view{studio, std::filesystem::path(dir.path().toStdWString())};
    ThreadController threads{studio, std::filesystem::path(dir.path().toStdWString())};
    AnalysisController analysis{studio, view, std::filesystem::path(dir.path().toStdWString())};
    GpuCanvas canvas;
    Rig() {
        canvas.setWidth(800);
        canvas.setHeight(600);
        canvas.setController(&studio);
        view.attachCanvas(&canvas);
        session.replaceProject(fixture(), true);
        wait();
    }
    void wait() {
        QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && studio.scene() != nullptr, 15'000);
        QTRY_VERIFY_WITH_TIMEOUT(view.simReady() && view.simTotal() > 0, 15'000);
    }
};
} // namespace

class StudioViewTest : public QObject {
    Q_OBJECT
private slots:
    void displayFiltersAreOneUndoStep();
    void zoomCommandsAndCalibration();
    void gridGuidesAndLayersPersist();
    void simulationNavigatesTheEffectiveSequence();
    void simulationPrefixMatchesReferenceAtEveryPosition();
    void metadataBatchDoesNotRebuildStitches();
    void threadPlanColorwaysAndSewingOrder();
    void diagnosticsDensityRiskAndOptimisation();
    void hoopsAndMachine();
    void initTestCase();
    void mainQmlHostsEveryPanel();
};

void StudioViewTest::displayFiltersAreOneUndoStep() {
    Rig rig;
    QVERIFY(!rig.view.showAll()); // nothing hidden: refusal with a reason
    QVERIFY(!rig.view.message().isEmpty());
    const auto all = rig.studio.scene()->segments.size();
    QVERIFY(all > 0);
    QVERIFY(rig.view.showOnlyColour(QStringLiteral("#b41141")));
    QCOMPARE(rig.view.hiddenCount(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(rig.studio.scene() && rig.studio.scene()->segments.size() < all,
                             15'000);
    QVERIFY(!rig.view.showOnlyColour(QStringLiteral("#010203"))); // no such thread
    QVERIFY(!rig.view.showOnlyColour(QStringLiteral("nope")));
    QVERIFY(rig.view.showAll());
    QCOMPARE(rig.view.hiddenCount(), 0);
    rig.session.undo(); // undoes showAll only
    QCOMPARE(rig.view.hiddenCount(), 1);
    rig.session.undo(); // undoes showOnlyColour
    QCOMPARE(rig.view.hiddenCount(), 0);
    QVERIFY(rig.view.showOnlyType(QStringLiteral("running")) ||
            !rig.view.message().isEmpty()); // both objects share the type: refusal or no-op
}

void StudioViewTest::zoomCommandsAndCalibration() {
    Rig rig;
    QVERIFY(!rig.view.zoomSelection()); // nothing selected
    QVERIFY(!rig.view.zoomPreviousView());
    rig.studio.selectObject(QString::number(firstId));
    const double fitted = rig.canvas.zoom();
    QVERIFY(rig.view.zoomSelection());
    QVERIFY(rig.canvas.zoom() > fitted);
    QVERIFY(rig.canvas.canZoomPrevious());
    QVERIFY(rig.view.zoomPreviousView());
    QVERIFY(std::abs(rig.canvas.zoom() - fitted) < 1e-6);
    rig.view.zoomActualSize();
    QVERIFY(std::abs(rig.canvas.zoom() - rig.view.pixelsPerMmAt100()) < 1e-6);
    // 100 mm shown at 3.7795 px/mm, measured 100 mm of real ruler -> px/mm = zoom.
    QVERIFY(rig.view.calibrateZoom100(50.0, 100.0));
    QVERIFY(std::abs(rig.view.pixelsPerMmAt100() - rig.canvas.zoom() * 2.0) < 1e-6);
    QVERIFY(!rig.view.calibrateZoom100(-1.0));
    rig.view.zoomRectangle();
    QVERIFY(rig.canvas.zoomRectMode());
    const auto ticks = rig.canvas.rulerTicks(true);
    QVERIFY(!ticks.isEmpty());
    rig.view.centerOnModel(0, 10);
    const auto c = rig.canvas.camera().worldToScreen({0, -10});
    QVERIFY(std::abs(c.x - 400) < 1e-6 && std::abs(c.y - 300) < 1e-6);
}

void StudioViewTest::gridGuidesAndLayersPersist() {
    QTemporaryDir dir;
    const auto path = std::filesystem::path(dir.path().toStdWString());
    {
        DocumentSession session;
        StudioController studio(session);
        ViewController view(studio, path);
        view.setGridVisible(true);
        view.setGridStepMm(5.0);
        view.setLayerJumps(true);
        view.setGridStepMm(0.1); // refused
        QVERIFY(!view.message().isEmpty());
        QVERIFY(view.addGuide(true, 12.0));
        QVERIFY(!view.addGuide(true, 12.0)); // duplicate
        QCOMPARE(view.guides().size(), 1);
        view.clearGuides();
        QCOMPARE(view.guides().size(), 0);
    }
    DocumentSession session;
    StudioController studio(session);
    ViewController view(studio, path);
    QVERIFY(view.gridVisible());
    QCOMPARE(view.gridStepMm(), 5.0);
    QVERIFY(view.layerJumps());
}

void StudioViewTest::simulationNavigatesTheEffectiveSequence() {
    Rig rig;
    const int total = rig.view.simTotal();
    QVERIFY(total > 2);
    rig.view.startSimulation();
    QVERIFY(rig.view.simActive());
    QCOMPARE(rig.view.simPosition(), 0);
    QCOMPARE(rig.canvas.overlays()->visibleSegments, std::size_t{0});
    rig.view.step(1);
    rig.view.step(1);
    rig.view.step(1);
    QCOMPARE(rig.view.simPosition(), 3);
    QVERIFY(rig.canvas.overlays()->visibleSegments <= 2);
    rig.view.step(-1);
    QCOMPARE(rig.view.simPosition(), 2);
    rig.view.nextObject(); // second object starts after the first
    QVERIFY(rig.view.simPosition() > 2);
    const auto atSecond = rig.canvas.overlays()->visibleSegments;
    QVERIFY(atSecond >= 1);
    rig.view.previousObject();
    QVERIFY(rig.view.simPosition() < total);
    rig.view.play();
    QVERIFY(rig.view.playing());
    const int before = rig.view.simPosition();
    rig.view.advance(500);
    QVERIFY(rig.view.simPosition() > before);
    rig.view.advance(600'000); // runs to the end and stops by itself
    QCOMPARE(rig.view.simPosition(), total);
    QVERIFY(!rig.view.playing());
    QCOMPARE(rig.canvas.overlays()->visibleSegments, CanvasOverlays::all);
    QVERIFY(!rig.view.simEstimate().isEmpty());
    rig.view.setSimPosition(1);
    QCOMPARE(rig.view.simPosition(), 1);
    QVERIFY(!rig.view.simTimeText().isEmpty());
    rig.view.stopSimulation();
    QVERIFY(!rig.view.simActive());
    QCOMPARE(rig.canvas.overlays()->visibleSegments, CanvasOverlays::all);
    rig.view.setMachineSpm(50); // refused
    QCOMPARE(rig.view.machineSpm(), 800);
}

void StudioViewTest::simulationPrefixMatchesReferenceAtEveryPosition() {
    Rig rig;
    const auto& project = *rig.studio.scene()->project;
    const auto sequence = stitch_generation::effective_sequence(project, TaskContext{});
    QVERIFY(sequence.has_value());
    QCOMPARE(rig.view.simTotal(), static_cast<int>(sequence->commands.size()));
    rig.view.startSimulation();
    for (std::size_t i = 0; i < sequence->commands.size(); ++i) {
        rig.view.setSimPosition(static_cast<int>(i));
        std::size_t reference = 0;
        if (i > 0) {
            const auto color = [](ObjectId) -> std::optional<std::array<std::uint8_t, 3>> {
                return std::array<std::uint8_t, 3>{0, 0, 0};
            };
            reference = stitch_render::build_thread_segments(*sequence, i - 1, color).size();
        }
        QCOMPARE(rig.canvas.overlays()->visibleSegments, reference);
    }
}

void StudioViewTest::metadataBatchDoesNotRebuildStitches() {
    Rig rig;
    const auto initial = rig.studio.scene();
    QSignalSpy scenes(&rig.studio, &StudioController::sceneChanged);
    const auto id = std::to_string(firstId);
    const std::string start = R"({"schema":"openstitch.batch/1","operations":[{)";
    const std::string end = "}]}";
    const std::string rename =
        start + R"("op":"rename","object":)" + id + R"(,"name":"Updated name")" + end;
    const auto renamed = rig.session.applyBatch(rename, "Rename");
    QVERIFY(renamed.committed);
    QVERIFY(!rig.session.lastChangeAffectsEmbroidery());
    QVERIFY(rig.studio.scene() == initial);
    QCOMPARE(scenes.count(), 0);
    QVERIFY(rig.session.undo());
    QVERIFY(rig.studio.scene() == initial);
    QVERIFY(rig.session.redo());
    QVERIFY(rig.studio.scene() == initial);
    QCOMPARE(scenes.count(), 0);

    // A real change to the stitched result must still invalidate the scene.
    const std::string hide =
        start + R"("op":"set_visible","object":)" + id + R"(,"visible":false)" + end;
    const auto hidden = rig.session.applyBatch(hide, "Hide");
    QVERIFY(hidden.committed);
    QVERIFY(rig.session.lastChangeAffectsEmbroidery());
    QVERIFY(scenes.count() > 0);
    QTRY_VERIFY_WITH_TIMEOUT(!rig.studio.busy() && rig.studio.scene(), 15'000);
    QVERIFY(rig.studio.scene() != initial);
}

void StudioViewTest::threadPlanColorwaysAndSewingOrder() {
    Rig rig;
    QCOMPARE(rig.threads.threadCount(), 2);
    QVERIFY(rig.threads.selectThreadObjects(0));
    QCOMPARE(rig.session.selection().targets().size(), std::size_t{1});
    QVERIFY(rig.threads.setThreadVisible(1, false));
    QCOMPARE(rig.view.hiddenCount(), 1);
    rig.session.undo();
    QCOMPARE(rig.view.hiddenCount(), 0);
    QVERIFY(!rig.threads.mergeThreads(0, 9)); // unknown thread: refused with a message
    QVERIFY(!rig.threads.message().isEmpty());
    QVERIFY(rig.threads.addColorway(QStringLiteral("Hiver")));
    QVERIFY(rig.threads.colorways().size() >= 1); // a base colourway may be captured too
    QVERIFY(!rig.threads.addColorway(QString())); // empty name refused
    QVERIFY(rig.threads.recolorThread(0, QStringLiteral("#112233")));
    QVERIFY(rig.threads.mergeThreads(1, 0));
    QCOMPARE(rig.threads.threadCount(), 1);
    rig.session.undo();
    rig.session.undo();
    QCOMPARE(rig.threads.threadCount(), 2);
    QVERIFY(rig.threads.moveInOrder(QString::number(secondId), 0));
    QCOMPARE(rig.threads.sewingOrder().first().toMap().value("id").toString(),
             QString::number(secondId));
    rig.session.undo();
    QCOMPARE(rig.threads.sewingOrder().first().toMap().value("id").toString(),
             QString::number(firstId));
    QVERIFY(!rig.threads.charts().isEmpty());
    const auto chart = rig.threads.charts().first().toMap().value("id").toString();
    const auto nearest = rig.threads.nearestToEntry(0, chart, 3);
    QVERIFY(!nearest.isEmpty());
    const auto code = nearest.first().toMap().value("code").toString();
    QVERIFY(rig.threads.assignChartThread(0, chart, code));
    QVERIFY(!rig.threads.assignChartThread(0, chart, QStringLiteral("no-such-code")));
    QVERIFY(!rig.threads.removeChart(chart)); // built-in charts cannot be removed
    QTemporaryDir out;
    QVERIFY(rig.threads.exportThreadList(
        QUrl::fromLocalFile(out.filePath(QStringLiteral("threads.csv")))));
    QVERIFY(QFileInfo::exists(out.filePath(QStringLiteral("threads.csv"))));
}

void StudioViewTest::diagnosticsDensityRiskAndOptimisation() {
    Rig rig;
    rig.analysis.runDiagnostics();
    QTRY_VERIFY_WITH_TIMEOUT(rig.analysis.diagnosticsReady(), 15'000);
    QVERIFY(!rig.analysis.diagnosticsStale());
    QVERIFY(!rig.analysis.diagnosticsSummary().isEmpty());
    rig.analysis.computeDensity();
    QTRY_VERIFY_WITH_TIMEOUT(rig.analysis.densityReady() || !rig.analysis.lastOk(), 15'000);
    rig.analysis.computeFabricRisk();
    QTRY_VERIFY_WITH_TIMEOUT(rig.analysis.riskReady() || !rig.analysis.lastOk(), 20'000);
    QTRY_VERIFY_WITH_TIMEOUT(!rig.analysis.busy(), 20'000);
    if (rig.analysis.densityReady()) {
        QVERIFY(!rig.canvas.overlays()->images.empty());
    }
    // Order proposal: stale after an edit, refused with a reason.
    rig.analysis.proposeOrder(1, false);
    QTRY_VERIFY_WITH_TIMEOUT(rig.analysis.proposalReady(), 15'000);
    QVERIFY(!rig.analysis.proposalSummary().isEmpty());
    QVERIFY(rig.studio.moveSelection(0, 0) == false);
    QVERIFY(rig.threads.recolorThread(0, QStringLiteral("#445566")));
    QVERIFY(rig.analysis.proposalStale());
    QVERIFY(!rig.analysis.applyProposal());
    QVERIFY(!rig.analysis.lastOk());
    // A maps never stays over a changed document.
    QVERIFY(!rig.analysis.densityReady());
    rig.analysis.proposeOrder(0, false);
    QTRY_VERIFY_WITH_TIMEOUT(rig.analysis.proposalReady() && !rig.analysis.proposalStale(), 15'000);
    const bool canApply = rig.analysis.proposalImproves();
    const auto stepsBefore = rig.session.canUndo();
    QVERIFY(stepsBefore);
    if (canApply) {
        QVERIFY(rig.analysis.applyProposal() || !rig.analysis.lastOk());
    }
    QVERIFY(!rig.analysis.centerOnFinding(99)); // out of range
}

void StudioViewTest::hoopsAndMachine() {
    Rig rig;
    const auto check = rig.analysis.checkHoop();
    QVERIFY(check.value("checked").toBool());
    QVERIFY(rig.analysis.applyLibraryHoop(QStringLiteral("rect-50x50")));
    const auto small = rig.analysis.checkHoop(); // 30 x 20.2 mm motif fits 50 x 50
    QVERIFY(small.value("fits").toBool());
    QVERIFY(rig.analysis.applyLibraryHoop(QStringLiteral("round-60")) || !rig.analysis.lastOk());
    QVERIFY(rig.analysis.saveCurrentHoopAs(QStringLiteral("Mon cadre")));
    QCOMPARE(rig.analysis.userHoops().size(), 1);
    QVERIFY(!rig.analysis.saveUserHoop(QStringLiteral("Bad;name"), 100, 100,
                                       QStringLiteral("rectangle"), 0, 0));
    QVERIFY(rig.analysis.applyLibraryHoop(QStringLiteral("rect-100x100")));
    QVERIFY(rig.analysis.applyUserHoop(QStringLiteral("Mon cadre")));
    QVERIFY(rig.analysis.removeUserHoop(QStringLiteral("Mon cadre")));
    QVERIFY(!rig.analysis.removeUserHoop(QStringLiteral("Mon cadre")));
    QVERIFY(!rig.analysis.applyLibraryHoop(QStringLiteral("nope")));
    QVERIFY(rig.analysis.setMachine(QStringLiteral("generic-single-100")));
    QCOMPARE(rig.analysis.machineId(), QStringLiteral("generic-single-100"));
    QVERIFY(!rig.analysis.setMachine(QStringLiteral("nope")));
    QVERIFY(rig.analysis.setRenderSetting(QStringLiteral("relief"), QStringLiteral("0.2")));
    QVERIFY(!rig.analysis.setRenderSetting(QStringLiteral("relief"), QStringLiteral("abc")));
    QVERIFY(rig.analysis.applyRenderPreset(QStringLiteral("draft")));
    QVERIFY(!rig.analysis.applyRenderPreset(QStringLiteral("nope")));
}

void StudioViewTest::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                 QStringLiteral("Provided by the application"));
}

namespace {
// Repeater delegates are not QObject children of the window: walk the visual tree.
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
// AbstractButton::click() runs the real onClicked handler even when the button is scrolled
// out of its Flickable (a mouse click there would land elsewhere).
bool press(QQuickWindow* window, const char* name) {
    auto* item = named(window, name);
    return item && item->isEnabled() && QMetaObject::invokeMethod(item, "click");
}
} // namespace

void StudioViewTest::mainQmlHostsEveryPanel() {
    DocumentSession session;
    StudioController studio(session);
    ActionController actions(studio);
    EditController editor(studio);
    QTemporaryDir dir;
    const auto cfg = std::filesystem::path(dir.path().toStdWString());
    FileController files(studio, &actions, dir.path());
    InspectorController inspector(studio);
    ViewController view(studio, cfg);
    ThreadController threads(studio, cfg);
    AnalysisController analysis(studio, view, cfg);
    QQmlApplicationEngine engine;
    QStringList warnings;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError>& errors) {
        for (const auto& e : errors) {
            warnings.push_back(e.toString());
        }
    });
    auto* ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("studio"), &studio);
    ctx->setContextProperty(QStringLiteral("actions"), &actions);
    ctx->setContextProperty(QStringLiteral("editor"), &editor);
    ctx->setContextProperty(QStringLiteral("files"), &files);
    ctx->setContextProperty(QStringLiteral("inspector"), &inspector);
    ctx->setContextProperty(QStringLiteral("view"), &view);
    ctx->setContextProperty(QStringLiteral("threads"), &threads);
    ctx->setContextProperty(QStringLiteral("analysis"), &analysis);
    engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    session.replaceProject(fixture(), true);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && studio.scene() != nullptr, 15'000);
    QTRY_VERIFY_WITH_TIMEOUT(view.simReady(), 15'000);
    auto* hub = named(window, "viewHub");
    QVERIFY(hub);
    QTRY_VERIFY(hub->property("ready").toBool());
    auto* canvas = window->findChild<GpuCanvas*>(QStringLiteral("studioCanvas"));
    QVERIFY(canvas);
    QVERIFY(canvas->overlays() != nullptr); // attachCanvas published the first overlays

    // Panel "Vue": filters, grid, guides, zoom.
    QVERIFY(press(window, "viewPanelButton"));
    QCOMPARE(hub->property("openPanel").toString(), QStringLiteral("view"));
    studio.selectObject(QString::number(firstId));
    QTRY_VERIFY(named(window, "onlySelectedColourButton")->isEnabled());
    QVERIFY(press(window, "onlySelectedColourButton"));
    QCOMPARE(view.hiddenCount(), 1);
    QTRY_VERIFY(named(window, "showAllButton")->isEnabled());
    QVERIFY(press(window, "showAllButton"));
    QCOMPARE(view.hiddenCount(), 0);
    QVERIFY(press(window, "gridBox"));
    QVERIFY(view.gridVisible());
    QVERIFY(canvas->overlays()->grid);
    QVERIFY(press(window, "rulersBox"));
    QVERIFY(view.rulersVisible());
    QVERIFY(press(window, "addVGuideButton"));
    QCOMPARE(view.guides().size(), 1);
    QCOMPARE(canvas->overlays()->guidesX.size(), std::size_t{1});
    QVERIFY(press(window, "zoom100Button"));
    QVERIFY(std::abs(canvas->zoom() - view.pixelsPerMmAt100()) < 1e-6);

    // Zoom rectangle through the real canvas gesture (one-shot rubber band).
    canvas->fitDesign();
    const double fitted = canvas->zoom();
    QVERIFY(press(window, "zoomRectButton"));
    QVERIFY(canvas->zoomRectMode());
    const auto origin = canvas->mapToScene(QPointF(0, 0)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, origin + QPoint(300, 200));
    QTest::mouseMove(window, origin + QPoint(350, 240));
    QVERIFY(canvas->rubberBand().width() > 0);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, origin + QPoint(400, 280));
    QVERIFY(canvas->zoom() > fitted);
    QVERIFY(!canvas->zoomRectMode());

    // Panel "Fils".
    QVERIFY(press(window, "threadsPanelButton"));
    QCOMPARE(hub->property("openPanel").toString(), QStringLiteral("threads"));
    // THR-003: fictional manufacturer charts must not be presented as production palettes.
    auto* demoWarning = named(window, "demoChartWarning");
    QVERIFY(demoWarning);
    QTRY_VERIFY(demoWarning->isVisible());
    QVERIFY(demoWarning->property("text").toString().contains(QStringLiteral("fictifs")));
    QTRY_VERIFY(named(window, "threadRow0") != nullptr);
    // Even in a narrow side drawer, wrapping controls must contribute their
    // actual height and remain inside the panel instead of being clipped.
    auto* threadPanel = named(window, "threadPanel");
    QVERIFY(threadPanel);
    for (const char* flowName :
         {"threadActionsFlow", "threadChartActionsFlow", "threadSearchActionsFlow",
          "threadColorwayActionsFlow"}) {
        auto* flow = named(window, flowName);
        QVERIFY2(flow, flowName);
        QTRY_VERIFY(flow->implicitHeight() >= flow->childrenRect().height() - 1.0);
    }
    for (const char* buttonName :
         {"threadRecolorButton", "nearestButton", "colorwayAddButton"}) {
        auto* button = named(window, buttonName);
        QVERIFY2(button, buttonName);
        const double x = button->mapToItem(threadPanel, QPointF(0, 0)).x();
        QVERIFY2(x >= -1 && x + button->width() <= threadPanel->width() + 1,
                 buttonName);
    }
    named(window, "colorwayNameField")->setProperty("text", QStringLiteral("Printemps"));
    QVERIFY(press(window, "colorwayAddButton"));
    QVERIFY(!threads.colorways().isEmpty());
    QVERIFY(press(window, "orderDown0"));
    QCOMPARE(threads.sewingOrder().first().toMap().value("id").toString(),
             QString::number(secondId));

    // Panel "Analyse".
    QVERIFY(press(window, "analysisPanelButton"));
    QCOMPARE(hub->property("openPanel").toString(), QStringLiteral("analysis"));
    // HOOP-001: the template selector must not pretend that the first library
    // hoop (50x50) is the current 100x100 project hoop.
    auto* activeHoop = named(window, "currentHoopSummary");
    auto* hoopSelector = named(window, "hoopBox");
    auto* applyHoop = named(window, "applyHoopButton");
    QVERIFY(activeHoop);
    QVERIFY(hoopSelector);
    QVERIFY(applyHoop);
    QTRY_VERIFY(activeHoop->property("text").toString().contains(QStringLiteral("100")));
    QCOMPARE(hoopSelector->property("currentIndex").toInt(), -1);
    QVERIFY(!applyHoop->isEnabled());
    QVERIFY(hoopSelector->setProperty("currentIndex", 0));
    QTRY_VERIFY(applyHoop->isEnabled());
    QVERIFY(press(window, "applyHoopButton"));
    QTRY_VERIFY(activeHoop->property("text").toString().contains(QStringLiteral("50")));

    QVERIFY(press(window, "diagnoseButton"));
    QTRY_VERIFY_WITH_TIMEOUT(analysis.diagnosticsReady(), 15'000);
    QVERIFY(press(window, "proposeButton"));
    QTRY_VERIFY_WITH_TIMEOUT(analysis.proposalReady(), 15'000);
    QVERIFY(press(window, "checkHoopButton"));
    QVERIFY(!analysis.message().isEmpty());

    // Simulation bar.
    QTRY_VERIFY(named(window, "simulationButton")->isEnabled());
    QVERIFY(press(window, "simulationButton"));
    QVERIFY(view.simActive());
    QTRY_VERIFY(named(window, "simNextStitch") != nullptr);
    QVERIFY(press(window, "simNextStitch"));
    QCOMPARE(view.simPosition(), 1);
    QVERIFY(press(window, "simPlayButton"));
    QVERIFY(view.playing());
    view.advance(600'000);
    QCOMPARE(view.simPosition(), view.simTotal());
    QVERIFY(press(window, "simToEnd"));
    QVERIFY(press(window, "simClose"));
    QVERIFY(!view.simActive());

    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

QTEST_MAIN(StudioViewTest)
#include "test_studio_view.moc"
