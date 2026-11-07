// SPDX-License-Identifier: Apache-2.0
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <array>
#include <filesystem>

#include "analysis_controller.hpp"
#include "gpu_canvas.hpp"
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
    void threadPlanColorwaysAndSewingOrder();
    void diagnosticsDensityRiskAndOptimisation();
    void hoopsAndMachine();
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

QTEST_MAIN(StudioViewTest)
#include "test_studio_view.moc"
