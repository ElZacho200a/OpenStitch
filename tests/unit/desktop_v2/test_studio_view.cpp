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
    void stockInventoryAndAtomicClosestThreadAssignment();
    void colorFilmReductionAndFabricAreUndoable();
    void diagnosticsDensityRiskAndOptimisation();
    void hoopsAndMachine();
    void simulationPauseStartEndAndColourJumps();
    void guidesAddRemoveAndColour();
    void analysisClearsDensityRiskAndDiscardsProposal();
    void chartImportAndOwnedThreadAssignment();
    void displayFiltersByTypeColourAndSize();
    void statusEventsDoNotReloadThreadsFiltersOrTree();
    void statisticsAndFidelityOfTheEmbroidery();
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

void StudioViewTest::stockInventoryAndAtomicClosestThreadAssignment() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto cfg = std::filesystem::path(directory.path().toStdWString());
    DocumentSession session;
    StudioController studio(session);
    session.replaceProject(fixture(), true);
    ThreadController threads(studio, cfg);
    const QString chart = threads.charts().first().toMap().value("id").toString();
    const auto nearest = threads.nearestToEntry(0, chart, 1);
    QVERIFY(!nearest.isEmpty());
    const QString code = nearest.first().toMap().value("code").toString();
    QVERIFY(threads.setOwnedThread(chart, code, true));
    QVERIFY(threads.ownsThread(chart, code));
    QVERIFY(!threads.setOwnedThread(chart, code, true)); // no duplicate
    const QString personalRef = QStringLiteral("MY-01");
    const QString personalName = QStringLiteral("Raspberry");
    const QString personalColor = QStringLiteral("#b41141");
    QVERIFY(threads.addPersonalThread(personalRef, personalName, personalColor));
    QCOMPARE(threads.stockCount(), 2);
    threads.setLimitToStock(true);
    QVERIFY(threads.limitToStock());
    const auto closest = threads.nearestToEntry(0, chart, 1);
    QCOMPARE(closest.first().toMap().value("code").toString(), QStringLiteral("MY-01"));

    const auto beforeUndo = session.undoNames().size();
    QCOMPARE(threads.snapFreeColours(chart), 2);
    QCOMPARE(session.undoNames().size(), beforeUndo + 1);
    QVERIFY(session.project().embroidery_objects[0].thread.has_value());
    QVERIFY(session.project().embroidery_objects[1].thread.has_value());
    QVERIFY(session.undo());
    QVERIFY(!session.project().embroidery_objects[0].thread.has_value());
    QVERIFY(!session.project().embroidery_objects[1].thread.has_value());

    // Stock is a user preference, not stored inside Project or the undo history.
    ThreadController reloaded(studio, cfg);
    QCOMPARE(reloaded.stockCount(), 2);
    QVERIFY(reloaded.limitToStock());
    QCOMPARE(reloaded.ownedThreads().size(), 2);
    QVERIFY(reloaded.removeOwnedThread(QStringLiteral("personal"), QStringLiteral("MY-01")));
    QCOMPARE(reloaded.stockCount(), 1);
    QVERIFY(reloaded.setOwnedThread(chart, code, false));
    QCOMPARE(reloaded.stockCount(), 0);
    QCOMPARE(threads.stockCount(), 2); // each instance caches its own preferences
}

void StudioViewTest::colorFilmReductionAndFabricAreUndoable() {
    Rig rig;
    auto blocks = rig.threads.colorFilm();
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(rig.threads.filmChanges(), 1);
    QVERIFY(rig.threads.selectFilmBlock(0));
    QCOMPARE(rig.session.selection().targets().size(), std::size_t{1});
    const auto oldFirst = rig.session.project().embroidery_objects.front().id;
    QVERIFY(rig.threads.moveFilmBlock(0, 1));
    QCOMPARE(rig.session.project().embroidery_objects.front().id, ObjectId{secondId});
    QVERIFY(rig.session.undo());
    QCOMPARE(rig.session.project().embroidery_objects.front().id, oldFirst);
    rig.wait();

    const auto preview = rig.threads.previewReduction(1);
    QCOMPARE(preview.value("before").toInt(), 2);
    QCOMPARE(preview.value("after").toInt(), 1);
    QCOMPARE(preview.value("affected").toInt(), 1);
    const auto before = rig.session.undoNames().size();
    QVERIFY(rig.threads.applyReduction());
    QCOMPARE(rig.session.undoNames().size(), before + 1);
    QVERIFY(!rig.threads.applyReduction()); // a preview can only be accepted once
    QVERIFY(rig.session.undo());
    QCOMPARE(rig.threads.threadCount(), 2);

    const auto original = rig.threads.fabric();
    QCOMPARE(original.value("color").value<QColor>(), QColor(Qt::white));
    QVERIFY(!rig.threads.setFabric(QStringLiteral("bad"), {}, {}, {}, 0, 1));
    const QString navy = QStringLiteral("#112233");
    const QString linen = QStringLiteral("Lin");
    const QString woven = QStringLiteral("woven");
    const QString stabilizer = QStringLiteral("cut-away");
    QVERIFY(rig.threads.setFabric(navy, linen, woven, stabilizer, 30, 1.25));
    QCOMPARE(rig.threads.fabric().value("name").toString(), QStringLiteral("Lin"));
    QCOMPARE(rig.session.project().fabric.material, std::string("woven"));
    QVERIFY(rig.session.undo());
    QCOMPARE(rig.threads.fabric().value("color").value<QColor>(), QColor(Qt::white));
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
    QVERIFY(named(window, "filterTypesFlow"));
    QVERIFY(named(window, "filterColoursFlow"));
    QVERIFY(named(window, "coloursAllButton") && named(window, "coloursNoneButton") &&
            named(window, "coloursInvertButton"));
    QVERIFY(named(window, "minAreaField") && named(window, "hideSmallButton"));
    QVERIFY(press(window, "coloursNoneButton"));
    QCOMPARE(view.hiddenCount(), static_cast<int>(session.project().embroidery_objects.size()));
    QTRY_VERIFY(named(window, "filterIndicator")->isVisible());
    QVERIFY(press(window, "coloursAllButton"));
    QCOMPARE(view.hiddenCount(), 0);
    // Réglages détaillés du rendu réaliste (V1) : curseurs, tissu, armure, qualité.
    QVERIFY(named(window, "renderAdvancedBox"));
    QVERIFY(press(window, "renderAdvancedBox"));
    auto* sheen = named(window, "renderParam_sheen");
    QVERIFY(sheen);
    QTRY_VERIFY(sheen->isVisible());
    QVERIFY(std::abs(sheen->property("value").toDouble() -
                     analysis.renderSettings().value("sheen").toDouble()) < 1e-6);
    QVERIFY(analysis.setRenderSetting(QStringLiteral("sheen"), QStringLiteral("0.9")));
    QTRY_VERIFY(std::abs(sheen->property("value").toDouble() - 0.9) < 1e-6);
    QVERIFY(named(window, "renderParam_px_per_mm") && named(window, "renderParam_thread_mm") &&
            named(window, "renderParam_relief") && named(window, "renderParam_twist") &&
            named(window, "renderParam_shadow") && named(window, "renderParam_fabric_relief"));
    QVERIFY(named(window, "renderFabricButton") && named(window, "renderFabricSwatch"));
    const bool weave = analysis.renderSettings().value("fabricWeave").toBool();
    QVERIFY(press(window, "renderWeaveBox"));
    QCOMPARE(analysis.renderSettings().value("fabricWeave").toBool(), !weave);
    QVERIFY(press(window, "renderQualityBox"));
    QCOMPARE(analysis.renderSettings().value("preset").toString().isEmpty() ||
                 !analysis.renderSettings().value("highQuality").toBool(),
             true);
    QVERIFY(press(window, "gridBox"));
    QVERIFY(view.gridVisible());
    QVERIFY(canvas->overlays()->grid);
    // F38-02: invalid/partial spacing must never change the model. A French
    // decimal comma is interpreted as a decimal separator, not a suffix.
    auto* gridStep = named(window, "gridStepField");
    QVERIFY(gridStep);
    const double originalGrid = view.gridStepMm();
    QVERIFY(gridStep->setProperty("text", QStringLiteral("2mm")));
    QTRY_VERIFY(gridStep->property("invalid").toBool());
    gridStep->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Return);
    QCOMPARE(view.gridStepMm(), originalGrid);
    QVERIFY(gridStep->setProperty("text", QStringLiteral("2,5")));
    gridStep->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(view.gridStepMm(), 2.5);
    QVERIFY(press(window, "rulersBox"));
    QVERIFY(view.rulersVisible());
    QVERIFY(press(window, "addVGuideButton"));
    QCOMPARE(view.guides().size(), 1);
    QCOMPARE(canvas->overlays()->guidesX.size(), std::size_t{1});
    // F38-02: malformed guide coordinates cannot enter the document's view
    // state; decimal comma coordinates are passed with their fractional part.
    auto* guideCoordinate = named(window, "guideField");
    auto* addVertical = named(window, "addVGuideButton");
    QVERIFY(guideCoordinate && addVertical);
    QVERIFY(guideCoordinate->setProperty("text", QStringLiteral("12,5mm")));
    QTRY_VERIFY(!addVertical->isEnabled());
    QCOMPARE(view.guides().size(), 1);
    QVERIFY(guideCoordinate->setProperty("text", QStringLiteral("12,5")));
    QTRY_VERIFY(addVertical->isEnabled());
    QVERIFY(press(window, "addVGuideButton"));
    QCOMPARE(view.guides().size(), 2);
    QCOMPARE(canvas->overlays()->guidesX.size(), std::size_t{2});
    // Supprimer un guide depuis la liste, puis colorer les guides.
    QTRY_VERIFY(named(window, "removeGuide_1") != nullptr);
    QVERIFY(press(window, "removeGuide_1"));
    QCOMPARE(view.guides().size(), 1);
    QTRY_COMPARE(canvas->overlays()->guidesX.size(), std::size_t{1});
    QSignalSpy guideColourChanged(&view, &ViewController::displayChanged);
    QVERIFY(press(window, "guideColor_1"));
    QCOMPARE(guideColourChanged.count(), 1);
    QVERIFY(press(window, "addVGuideButton")); // le test suivant attend deux guides
    QCOMPARE(view.guides().size(), 2);
    QTRY_COMPARE(canvas->overlays()->guidesX.size(), std::size_t{2});
    QVERIFY(press(window, "zoom100Button"));
    QVERIFY(std::abs(canvas->zoom() - view.pixelsPerMmAt100()) < 1e-6);
    // F38-04: a physical calibration is only actionable for a complete
    // measured length, and accepts decimal comma input.
    auto* calibration = named(window, "calibrateField");
    auto* calibrationButton = named(window, "calibrateButton");
    QVERIFY(calibration && calibrationButton);
    QVERIFY(!calibrationButton->isEnabled());
    QVERIFY(calibration->setProperty("text", QStringLiteral("20mm")));
    QTRY_VERIFY(!calibrationButton->isEnabled());
    QVERIFY(calibration->setProperty("text", QStringLiteral("100,0")));
    QTRY_VERIFY(calibrationButton->isEnabled());
    QVERIFY(press(window, "calibrateButton"));
    QVERIFY(view.pixelsPerMmAt100() > 0);

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

    // Panel "Fils": all V1 features are now accessible through focused tabs.
    QVERIFY(press(window, "threadsPanelButton"));
    QCOMPARE(hub->property("openPanel").toString(), QStringLiteral("threads"));
    auto* threadPanel = named(window, "threadPanel");
    QVERIFY(threadPanel);
    QVERIFY(press(window, "threadTab_catalog"));
    auto* demoWarning = named(window, "demoChartWarning");
    QVERIFY(demoWarning);
    QTRY_VERIFY(demoWarning->isVisible());
    QVERIFY(demoWarning->property("text").toString().contains(QStringLiteral("fictifs")));
    for (const char* flowName : {"threadChartActionsFlow", "threadSearchActionsFlow"}) {
        auto* flow = named(window, flowName);
        QVERIFY2(flow, flowName);
        QTRY_VERIFY(flow->height() >= flow->childrenRect().height() - 1.0);
    }
    QVERIFY(named(window, "nearestButton"));

    QVERIFY(press(window, "threadTab_plan"));
    QTRY_VERIFY(named(window, "threadRow0") != nullptr);
    auto* projectActions = named(window, "threadActionsFlow");
    QVERIFY(projectActions);
    QTRY_VERIFY(projectActions->height() >= projectActions->childrenRect().height() - 1.0);
    QVERIFY(named(window, "threadReductionPreview"));

    QVERIFY(press(window, "threadTab_stock"));
    QVERIFY(named(window, "threadPersonalAdd"));
    QVERIFY(named(window, "threadLimitToStock"));

    QVERIFY(press(window, "threadTab_film"));
    QVERIFY(named(window, "filmSelect0"));
    QVERIFY(named(window, "filmGroupByColor"));

    QVERIFY(press(window, "threadTab_colorways"));
    auto* ways = named(window, "threadColorwayActionsFlow");
    QVERIFY(ways);
    QTRY_VERIFY(ways->height() >= ways->childrenRect().height() - 1.0);
    named(window, "colorwayNameField")->setProperty("text", QStringLiteral("Printemps"));
    QVERIFY(press(window, "colorwayAddButton"));
    QVERIFY(!threads.colorways().isEmpty());
    QVERIFY(named(window, "colorwayRename0"));

    // F37-02: a single-key View shortcut must not steal letters while the user
    // edits a text field; once the canvas regains focus the same shortcut works.
    auto* textEntry = named(window, "colorwayNameField");
    auto* pointsCanvas = named(window, "studioCanvas");
    QVERIFY(textEntry && pointsCanvas);
    textEntry->forceActiveFocus();
    QTRY_VERIFY(textEntry->hasActiveFocus());
    const bool gridInitiallyVisible = view.gridVisible();
    QTest::keyClick(window, Qt::Key_G);
    QCOMPARE(view.gridVisible(), gridInitiallyVisible);
    pointsCanvas->forceActiveFocus();
    QTRY_VERIFY(pointsCanvas->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_G);
    QTRY_COMPARE(view.gridVisible(), !gridInitiallyVisible);

    QVERIFY(press(window, "threadTab_fabric"));
    QVERIFY(named(window, "threadFabricApply"));
    QVERIFY(named(window, "threadFabricColor"));

    QVERIFY(press(window, "threadTab_order"));
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
    // F29-02: transport remains reachable on narrow canvases, and buttons
    // communicate the true bounds of the stitch sequence.
    auto* simBar = named(window, "simulationBar");
    auto* scroll = named(window, "simulationTransportScroll");
    QVERIFY(simBar && scroll);
    QVERIFY(simBar->setProperty("width", 420.0));
    QTRY_VERIFY(scroll->property("contentWidth").toDouble() > scroll->width());
    QTRY_VERIFY(!named(window, "simPrevStitch")->isEnabled());
    QVERIFY(press(window, "simNextStitch"));
    QCOMPARE(view.simPosition(), 1);
    QVERIFY(press(window, "simPlayButton"));
    QVERIFY(view.playing());
    view.advance(600'000);
    QCOMPARE(view.simPosition(), view.simTotal());
    QTRY_VERIFY(!named(window, "simNextStitch")->isEnabled());
    QTRY_VERIFY(!named(window, "simToEnd")->isEnabled());
    view.setSimPosition(0);
    QTRY_VERIFY(named(window, "simToEnd")->isEnabled());
    QVERIFY(press(window, "simToEnd"));
    QVERIFY(press(window, "simClose"));
    QVERIFY(!view.simActive());

    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

// pause / début / fin / couleur suivante et précédente : la lecture s'arrête et la position suit.
void StudioViewTest::simulationPauseStartEndAndColourJumps() {
    Rig rig;
    rig.view.startSimulation();
    rig.view.play();
    QVERIFY(rig.view.playing());
    rig.view.pause();
    QVERIFY(!rig.view.playing());
    rig.view.toEnd();
    QCOMPARE(rig.view.simPosition(), rig.view.simTotal());
    rig.view.toStart();
    QCOMPARE(rig.view.simPosition(), 0);
    QVERIFY(!rig.view.playing());
    rig.view.nextColour();
    const int afterNext = rig.view.simPosition();
    QVERIFY(afterNext > 0);
    QVERIFY(!rig.view.playing());
    rig.view.previousColour();
    QVERIFY(rig.view.simPosition() < afterNext);
    QVERIFY(rig.view.simPosition() >= 0);
}

// Repères : ajout, suppression par index (index invalide refusé), couleur (signal d'affichage).
void StudioViewTest::guidesAddRemoveAndColour() {
    Rig rig;
    const auto before = rig.view.guides().size();
    QVERIFY(rig.view.addGuide(true, 10.0));
    QCOMPARE(rig.view.guides().size(), before + 1);
    QVERIFY(!rig.view.removeGuide(99));
    QVERIFY(!rig.view.removeGuide(-1));
    QCOMPARE(rig.view.guides().size(), before + 1);
    QSignalSpy changed(&rig.view, &ViewController::displayChanged);
    rig.view.setGuideColor(QColor(QStringLiteral("#ff0000")));
    QCOMPARE(changed.count(), 1);
    rig.view.setGuideColor(QColor(QStringLiteral("#ff0000"))); // même couleur : aucun signal
    QCOMPARE(changed.count(), 1);
    QVERIFY(rig.view.removeGuide(static_cast<int>(before)));
    QCOMPARE(rig.view.guides().size(), before);
}

// Effacer la carte de densité / de risque et rejeter une proposition d'ordre : l'état publié
// repasse à « non prêt » avec son signal, sans toucher au document.
void StudioViewTest::analysisClearsDensityRiskAndDiscardsProposal() {
    Rig rig;
    const auto depth = rig.session.undoNames().size();
    rig.analysis.computeDensity();
    QTRY_VERIFY_WITH_TIMEOUT(rig.analysis.densityReady(), 15'000);
    QSignalSpy density(&rig.analysis, &AnalysisController::densityChanged);
    rig.analysis.clearDensity();
    QVERIFY(!rig.analysis.densityReady());
    QCOMPARE(density.count(), 1);
    rig.analysis.computeFabricRisk();
    QTRY_VERIFY_WITH_TIMEOUT(rig.analysis.riskReady(), 15'000);
    QSignalSpy risk(&rig.analysis, &AnalysisController::riskChanged);
    rig.analysis.clearFabricRisk();
    QVERIFY(!rig.analysis.riskReady());
    QCOMPARE(risk.count(), 1);
    rig.analysis.proposeOrder(0, false);
    QTRY_VERIFY_WITH_TIMEOUT(rig.analysis.proposalReady(), 15'000);
    rig.analysis.discardProposal();
    QVERIFY(!rig.analysis.proposalReady());
    QCOMPARE(rig.session.undoNames().size(), depth);
}

// Import d'un nuancier (refus d'une URL distante ou d'un fichier absent) et assignation d'un fil
// possédé à un fil du projet : une étape d'annulation, refus hors de « Mes fils ».
void StudioViewTest::chartImportAndOwnedThreadAssignment() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto cfg = std::filesystem::path(directory.path().toStdWString());
    DocumentSession session;
    StudioController studio(session);
    session.replaceProject(fixture(), true);
    ThreadController threads(studio, cfg);

    QVERIFY(!threads.importChart(QUrl(QStringLiteral("https://example.org/chart.csv"))));
    QVERIFY(
        !threads.importChart(QUrl::fromLocalFile(directory.filePath(QStringLiteral("none.csv")))));
    const QString csvPath = directory.filePath(QStringLiteral("mes_fils.csv"));
    QFile csv(csvPath);
    QVERIFY(csv.open(QIODevice::WriteOnly | QIODevice::Text));
    csv.write("code,name,hex\n1001,Rouge vif,#C8102E\n1002,Bleu nuit,#0B1F4D\n");
    csv.close();
    const auto chartsBefore = threads.charts().size();
    QSignalSpy changed(&threads, &ThreadController::chartsChanged);
    QVERIFY(threads.importChart(QUrl::fromLocalFile(csvPath)));
    QCOMPARE(threads.charts().size(), chartsBefore + 1);
    QVERIFY(changed.count() >= 1);

    const QString chart = threads.charts().first().toMap().value("id").toString();
    const QString code =
        threads.nearestToEntry(0, chart, 1).first().toMap().value("code").toString();
    QVERIFY(!threads.assignOwnedThread(0, chart, code)); // pas dans Mes fils
    QVERIFY(!threads.assignOwnedThread(-1, chart, code));
    QVERIFY(!threads.assignOwnedThread(999, chart, code));
    QVERIFY(threads.setOwnedThread(chart, code, true));
    const auto depth = session.undoNames().size();
    QVERIFY(threads.assignOwnedThread(0, chart, code));
    QCOMPARE(session.undoNames().size(), depth + 1);
    QVERIFY(session.project().embroidery_objects[0].thread.has_value());
    QVERIFY(session.undo());
    QVERIFY(!session.project().embroidery_objects[0].thread.has_value());
}

// Filtres d'affichage riches (V1) : types de points, couleurs de fil (Toutes / Aucune / Inverser)
// et taille minimale des zones ; chaque action est une étape d'annulation et l'état vient des
// objets.
void StudioViewTest::displayFiltersByTypeColourAndSize() {
    Rig rig;
    const auto total = static_cast<int>(rig.session.project().embroidery_objects.size());
    QVERIFY(total >= 2);
    const auto types = rig.view.filterTypes();
    const auto colours = rig.view.filterColours();
    QVERIFY(!types.isEmpty() && !colours.isEmpty());
    int typeSum = 0;
    for (const auto& t : types) {
        typeSum += t.toMap().value("total").toInt();
    }
    int colourSum = 0;
    for (const auto& c : colours) {
        colourSum += c.toMap().value("total").toInt();
    }
    QCOMPARE(typeSum, total);
    QCOMPARE(colourSum, total);
    QCOMPARE(rig.view.hiddenCount(), 0);

    const auto depth = rig.session.undoNames().size();
    const QString hex = colours.first().toMap().value("id").toString();
    const int colourTotal = colours.first().toMap().value("total").toInt();
    QVERIFY(rig.view.setColourVisible(hex, false));
    QCOMPARE(rig.session.undoNames().size(), depth + 1);
    QCOMPARE(rig.view.hiddenCount(), colourTotal);
    QVERIFY(!rig.view.filterColours().first().toMap().value("checked").toBool());
    QVERIFY(!rig.view.setColourVisible(hex, false)); // déjà masquée : refus lisible
    QVERIFY(!rig.view.setColourVisible(QStringLiteral("pas-une-couleur"), false));
    QVERIFY(rig.view.setColourVisible(hex, true));
    QCOMPARE(rig.view.hiddenCount(), 0);

    const QString typeId = types.first().toMap().value("id").toString();
    const int typeTotal = types.first().toMap().value("total").toInt();
    QVERIFY(rig.view.setTypeVisible(typeId, false));
    QCOMPARE(rig.view.hiddenCount(), typeTotal);
    QVERIFY(rig.session.undo());
    QCOMPARE(rig.view.hiddenCount(), 0);
    QVERIFY(!rig.view.setTypeVisible(QStringLiteral("type-inconnu"), false));

    QVERIFY(rig.view.setAllColours(QStringLiteral("none")));
    QCOMPARE(rig.view.hiddenCount(), total);
    QVERIFY(!rig.view.setAllColours(QStringLiteral("none")));
    QVERIFY(rig.view.setAllColours(QStringLiteral("invert")));
    QCOMPARE(rig.view.hiddenCount(), 0);
    QVERIFY(!rig.view.setAllColours(QStringLiteral("all")));
    QVERIFY(!rig.view.setAllColours(QStringLiteral("n-importe-quoi")));

    QVERIFY(!rig.view.hideSmallerThan(0.0));
    QVERIFY(!rig.view.hideSmallerThan(-3.0));
    QVERIFY(!rig.view.hideSmallerThan(1.0e-9)); // aucune zone aussi petite
}

// Un événement de statut (survol, progression, message) émet StudioController::changed sans rien
// changer au document : les listes de fils, les filtres et l'arbre d'objets ne sont PAS rediffusés
// (sinon QML les recalcule à chaque événement). Un vrai changement les rediffuse.
void StudioViewTest::statusEventsDoNotReloadThreadsFiltersOrTree() {
    Rig rig;
    EditController editor(rig.studio);
    QSignalSpy threads(&rig.threads, &ThreadController::changed);
    QSignalSpy view(&rig.view, &ViewController::changed);
    QSignalSpy tree(&editor, &EditController::treeChanged);
    for (int i = 0; i < 5; ++i) {
        emit rig.studio.changed();
    }
    QCOMPARE(threads.count(), 0);
    QCOMPARE(view.count(), 0);
    QCOMPARE(tree.count(), 0);

    const auto id = rig.session.project().embroidery_objects.front().id.value;
    rig.studio.selectObject(QString::number(id)); // la sélection change vraiment
    QTRY_VERIFY(threads.count() >= 1);
    QVERIFY(view.count() >= 1);
    QVERIFY(tree.count() >= 1);
    threads.clear();
    view.clear();
    tree.clear();
    QVERIFY(rig.view.setColourVisible(
        rig.view.filterColours().first().toMap().value("id").toString(), false));
    QTRY_VERIFY(threads.count() >= 1); // le document a changé
    QVERIFY(view.count() >= 1);
    QVERIFY(tree.count() >= 1);
}

// Statistiques de broderie (V1) : chiffres de la séquence effective ; fidélité à l'image : mesure
// asynchrone, déclarée périmée dès que le motif change, effaçable.
void StudioViewTest::statisticsAndFidelityOfTheEmbroidery() {
    Rig rig;
    const auto stats = rig.analysis.statistics();
    QVERIFY(stats.value("ready").toBool());
    QVERIFY(stats.value("stitches").toULongLong() > 0);
    QVERIFY(stats.value("widthMm").toDouble() > 0.0);
    QVERIFY(stats.value("threadM").toDouble() > 0.0);
    QVERIFY(stats.value("text").toString().contains(QStringLiteral("Points :")));
    QVERIFY(stats.value("text").toString().contains(QStringLiteral("Fil cousu estimé")));

    QVERIFY(!rig.analysis.fidelityReady());
    rig.analysis.measureFidelity();
    QTRY_VERIFY_WITH_TIMEOUT(rig.analysis.fidelityReady() || !rig.analysis.lastOk(), 30'000);
    if (rig.analysis.lastOk()) {
        QVERIFY(!rig.analysis.fidelityText().isEmpty());
        QVERIFY(!rig.analysis.fidelityStale());
        const auto id = rig.session.project().embroidery_objects.front().id.value;
        QVERIFY(rig.view.setColourVisible(
            QColor(rig.session.project().embroidery_objects.front().rgb[0],
                   rig.session.project().embroidery_objects.front().rgb[1],
                   rig.session.project().embroidery_objects.front().rgb[2])
                .name(),
            false));
        Q_UNUSED(id);
        QTRY_VERIFY(rig.analysis.fidelityStale());
        rig.analysis.clearFidelity();
        QVERIFY(!rig.analysis.fidelityReady());
    }
}

QTEST_MAIN(StudioViewTest)
#include "test_studio_view.moc"
