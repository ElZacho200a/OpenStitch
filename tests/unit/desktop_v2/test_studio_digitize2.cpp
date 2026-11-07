// SPDX-License-Identifier: Apache-2.0
// Studio -- famille I : gestes de numerisation au canevas (points, fondu, satin, guides),
// compensation de tirage et applique. Vrai shell QML, VRAI canevas (QTest::mouse*), vrai moteur.
// Chaque geste : une etape d'annulation, Echap sans mutation, retour octet pour octet (DST) apres
// annulation.
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
#include "applique_controller.hpp"
#include "digitize_gestures.hpp"
#include "edit_controller.hpp"
#include "fade_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "inspector_controller.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "point_edit_controller.hpp"
#include "pull_controller.hpp"
#include "satin_controller.hpp"
#include "shape_batch.hpp"
#include "studio_controller.hpp"
#include "thread_controller.hpp"
#include "view_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

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
    PointEditController points{studio};
    DigitizeGestures digitizer{studio, points, fade, satin};
    PullController pull{studio, &inspector};
    AppliqueController applique{studio, inspector};
    QQmlApplicationEngine engine;
    QStringList warnings;
    QQuickWindow* window{nullptr};
    GpuCanvas* canvas{nullptr};
    std::uint64_t id{0};
    [[nodiscard]] std::size_t steps() const { return session.undoNames().size(); }
};

std::vector<std::uint8_t> dstBytes(const DocumentSession& session) {
    auto sequence = stitch_generation::effective_sequence(session.project());
    if (!sequence) {
        return {};
    }
    auto bytes = formats::encode_dst(*sequence);
    return bytes ? *bytes : std::vector<std::uint8_t>{};
}

stitch::StitchSequence sequenceOf(const DocumentSession& session) {
    auto sequence = stitch_generation::effective_sequence(session.project());
    return sequence ? *sequence : stitch::StitchSequence{};
}

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
} // namespace

class StudioDigitize2Test : public QObject {
    Q_OBJECT
private:
    void open(Rig& rig, bool rect = true);
    [[nodiscard]] QPoint at(Rig& rig, double xMm, double yModelMm) {
        const auto s = rig.canvas->camera().worldToScreen({xMm, -yModelMm});
        return rig.canvas->mapToScene(QPointF(s.x, s.y)).toPoint();
    }
    void drag(Rig& rig, QPoint from, QPoint to, Qt::KeyboardModifiers mods = {},
              bool release = true) {
        QTest::mousePress(rig.window, Qt::LeftButton, mods, from);
        for (int step = 1; step <= 6; ++step) {
            QTest::mouseMove(rig.window, from + (to - from) * step / 6);
        }
        if (release) {
            QTest::mouseRelease(rig.window, Qt::LeftButton, mods, to);
        }
    }
    [[nodiscard]] int movablePoint(Rig& rig) {
        const auto all = rig.points.pointsIn(-1000, -1000, 1000, 1000, 0);
        // un point cousu isole : les voisins ne doivent pas etre a moins de 12 px
        for (const auto& v : all) {
            const auto m = v.toMap();
            if (!m.value("movable").toBool()) {
                continue;
            }
            const double x = m.value("x").toDouble();
            const double y = m.value("y").toDouble();
            const double tol = 9.0 / rig.canvas->camera().scale();
            if (rig.points.pointAt(x, y, tol) == m.value("index").toInt() && std::abs(x) < 12 &&
                std::abs(y) < 6) {
                return m.value("index").toInt();
            }
        }
        return -1;
    }

private slots:
    void initTestCase();
    void pointGesturesByMouse();
    void fadeHandlesByMouse();
    void satinHandlesAndRungsByMouse();
    void guideStrokeByMouse();
    void pullAndCalibration();
    void appliqueConvertAndCut();
    void overlayIsGroupedAndNonMutating();
};

void StudioDigitize2Test::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                 QStringLiteral("Provided by the application"));
    qmlRegisterUncreatableType<EditController>("OpenStitch.Studio", 1, 0, "EditController",
                                               QStringLiteral("Provided by the application"));
    qmlRegisterUncreatableType<DigitizeGestures>("OpenStitch.Studio", 1, 0, "DigitizeGestures",
                                                 QStringLiteral("Provided by the application"));
}

void StudioDigitize2Test::open(Rig& rig, bool rect) {
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
    ctx->setContextProperty(QStringLiteral("pointEdit"), &rig.points);
    ctx->setContextProperty(QStringLiteral("digitizer"), &rig.digitizer);
    ctx->setContextProperty(QStringLiteral("pull"), &rig.pull);
    ctx->setContextProperty(QStringLiteral("applique"), &rig.applique);
    rig.engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
    QVERIFY2(!rig.engine.rootObjects().isEmpty(), qPrintable(rig.warnings.join('\n')));
    rig.window = qobject_cast<QQuickWindow*>(rig.engine.rootObjects().front());
    QVERIFY(rig.window);
    rig.canvas = rig.window->findChild<GpuCanvas*>(QStringLiteral("studioCanvas"));
    QVERIFY(rig.canvas);
    auto project = project_ops::create_empty_project({});
    QVERIFY(project.has_value());
    rig.session.replaceProject(std::move(*project), true);
    if (rect) {
        QVERIFY(rig.session.applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle").committed);
        rig.id = rig.session.project().embroidery_objects.front().id.value;
    }
    QTRY_VERIFY_WITH_TIMEOUT(!rig.studio.busy() && rig.studio.scene() &&
                                 rig.studio.scene()->revision == rig.session.revision(),
                             15'000);
    QTRY_VERIFY_WITH_TIMEOUT(rig.canvas->geometryBuildCount() > 0, 10'000);
    rig.canvas->fitDesign();
    rig.canvas->forceActiveFocus();
    if (rect) {
        rig.studio.selectObject(QString::number(rig.id));
    }
}

void StudioDigitize2Test::pointGesturesByMouse() {
    Rig rig;
    open(rig);
    const auto baseDst = dstBytes(rig.session);
    QVERIFY(!baseDst.empty());
    const auto base = sequenceOf(rig.session);
    rig.digitizer.setMode(QStringLiteral("points"));
    QVERIFY(rig.points.active());
    QTRY_VERIFY(rig.points.pointCount() > 8);
    QVERIFY(rig.digitizer.capturesMouse());
    // entering the mode never changes the produced bytes
    QVERIFY(dstBytes(rig.session) == baseDst);

    const int index = movablePoint(rig);
    QVERIFY(index >= 0);
    const auto info = rig.points.pointInfo(index);
    const double px = info.value("x").toDouble(), py = info.value("y").toDouble();
    const auto depth = rig.steps();

    // click selects one point, no mutation
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, px, py));
    QCOMPARE(rig.points.selectedCount(), 1);
    QCOMPARE(rig.steps(), depth);

    // Escape during a drag: nothing changes
    drag(rig, at(rig, px, py), at(rig, px + 2, py + 1), {}, false);
    QVERIFY(rig.digitizer.busy());
    QVERIFY(rig.canvas->cancelGesture());
    QTest::mouseRelease(rig.window, Qt::LeftButton, {}, at(rig, px + 2, py + 1));
    QCOMPARE(rig.steps(), depth);
    QVERIFY(dstBytes(rig.session) == baseDst);

    // real drag: ONE undo step, sequence changes, undo restores the DST byte for byte
    drag(rig, at(rig, px, py), at(rig, px + 1.5, py + 1.0));
    QCOMPARE(rig.steps(), depth + 1);
    QTRY_COMPARE(rig.points.state(), QStringLiteral("edited"));
    QVERIFY(sequenceOf(rig.session).commands != base.commands);
    QVERIFY(dstBytes(rig.session) != baseDst);
    QVERIFY(rig.session.undo());
    QTRY_COMPARE(rig.points.state(), QStringLiteral("clean"));
    QVERIFY(sequenceOf(rig.session).commands == base.commands);
    QVERIFY(dstBytes(rig.session) == baseDst);

    // rectangle selection of several points, then Delete key deletes them in one step
    drag(rig, at(rig, -21.5, 11), at(rig, 0, -11));
    QVERIFY(rig.points.selectedCount() > 3);
    const auto depth2 = rig.steps();
    QTest::keyClick(rig.window, Qt::Key_Delete);
    QCOMPARE(rig.steps(), depth2 + 1);
    QVERIFY(dstBytes(rig.session) != baseDst);
    QVERIFY(rig.session.undo());
    QVERIFY(dstBytes(rig.session) == baseDst);

    // Alt+click inserts a point (one step)
    if (rig.points.objectId().isEmpty()) {
        rig.studio.selectObject(QString::number(rig.id)); // undo may drop the selection
    }
    QTRY_VERIFY(rig.points.pointCount() > 8);
    const auto depth3 = rig.steps();
    QTest::mouseClick(rig.window, Qt::LeftButton, Qt::AltModifier, at(rig, px + 0.1, py + 0.9));
    QVERIFY2(rig.steps() == depth3 + 1, qPrintable(rig.points.message()));
    QVERIFY(rig.session.undo());
    QVERIFY(dstBytes(rig.session) == baseDst);
    rig.digitizer.setMode(QString());
    QVERIFY(!rig.points.active());
}

void StudioDigitize2Test::fadeHandlesByMouse() {
    Rig rig;
    open(rig);
    QVERIFY(rig.fade.create(QColor(200, 30, 30), true));
    QTRY_VERIFY(rig.fade.hasFade());
    const auto base = dstBytes(rig.session);
    const auto depth = rig.steps();
    rig.studio.selectObject(
        QString::number(rig.session.project().embroidery_objects.front().id.value));
    rig.digitizer.setMode(QStringLiteral("fade"));
    const auto axis = rig.fade.axis();
    QVERIFY(!axis.isEmpty());
    QVERIFY(dstBytes(rig.session) == base); // overlay only
    const auto start = axis.value("start").toMap();
    const double sx = start.value("x").toDouble(), sy = start.value("y").toDouble();
    // Escape: no mutation
    drag(rig, at(rig, sx, sy), at(rig, sx + 3, sy), {}, false);
    QVERIFY(rig.canvas->cancelGesture());
    QTest::mouseRelease(rig.window, Qt::LeftButton, {}, at(rig, sx + 3, sy));
    QCOMPARE(rig.steps(), depth);
    // drag the start handle: ONE step
    drag(rig, at(rig, sx, sy), at(rig, sx + 3, sy));
    QCOMPARE(rig.steps(), depth + 1);
    QVERIFY(dstBytes(rig.session) != base);
    QVERIFY(rig.session.undo());
    QVERIFY(dstBytes(rig.session) == base);
    // reverse the direction: one step, undone exactly
    QVERIFY(rig.fade.reverse());
    QCOMPARE(rig.steps(), depth + 1);
    // the axis now runs the other way round
    const auto flipped = rig.fade.axis();
    QCOMPARE(flipped.value("start").toMap().value("x").toDouble(),
             axis.value("end").toMap().value("x").toDouble());
    QCOMPARE(flipped.value("end").toMap().value("y").toDouble(),
             axis.value("start").toMap().value("y").toDouble());
    QVERIFY(rig.session.undo());
    QVERIFY(dstBytes(rig.session) == base);
}

void StudioDigitize2Test::satinHandlesAndRungsByMouse() {
    Rig rig;
    open(rig, false);
    rig.digitizer.setMode(QStringLiteral("rungs"));
    const auto depth = rig.steps();
    drag(rig, at(rig, -10, 0), at(rig, -10, 4));
    QCOMPARE(rig.digitizer.pendingRungs(), 1);
    QVERIFY(!rig.digitizer.finishRungs()); // refused: two rungs needed, no mutation
    QCOMPARE(rig.steps(), depth);
    drag(rig, at(rig, 0, 1), at(rig, 0, 5));
    drag(rig, at(rig, 10, 0), at(rig, 10, 4));
    QCOMPARE(rig.digitizer.pendingRungs(), 3);
    QTest::keyClick(rig.window, Qt::Key_Return);
    QCOMPARE(rig.steps(), depth + 1);
    QTRY_VERIFY(rig.satin.isSatin());
    QCOMPARE(rig.satin.railNodesA(), 3);

    const auto base = dstBytes(rig.session);
    rig.digitizer.setMode(QStringLiteral("satin"));
    QVERIFY(dstBytes(rig.session) == base);
    const auto depth2 = rig.steps();
    drag(rig, at(rig, 0, 1), at(rig, 0, 2.5)); // rail A node 1
    QCOMPARE(rig.steps(), depth2 + 1);
    QVERIFY(dstBytes(rig.session) != base);
    QVERIFY(rig.session.undo());
    QVERIFY(dstBytes(rig.session) == base);
    // Alt+click on a rail segment inserts a node
    QTest::mouseClick(rig.window, Qt::LeftButton, Qt::AltModifier, at(rig, -5, 0.5));
    QCOMPARE(rig.steps(), depth2 + 1);
    QTRY_COMPARE(rig.satin.railNodesA(), 4);
    QVERIFY(rig.session.undo());
    QTRY_COMPARE(rig.satin.railNodesA(), 3);
    QVERIFY(dstBytes(rig.session) == base);
}

void StudioDigitize2Test::guideStrokeByMouse() {
    Rig rig;
    open(rig);
    const auto base = dstBytes(rig.session);
    rig.digitizer.setMode(QStringLiteral("guide"));
    const auto depth = rig.steps();
    drag(rig, at(rig, -12, -3), at(rig, 12, 3));
    QCOMPARE(rig.steps(), depth + 1); // conversion + guide in ONE step
    QTRY_VERIFY(rig.satin.guideCount() >= 1);
    QVERIFY(rig.session.undo());
    QVERIFY(dstBytes(rig.session) == base);
    rig.digitizer.setMode(QStringLiteral("break"));
    drag(rig, at(rig, 0, -8), at(rig, 0, 8));
    QCOMPARE(rig.steps(), depth + 1);
    QTRY_VERIFY(rig.satin.breakLineCount() >= 1);
}

void StudioDigitize2Test::pullAndCalibration() {
    Rig rig;
    open(rig);
    QCOMPARE(rig.pull.modes().size(), 4);
    QVERIFY(rig.pull.compare());
    QVERIFY(!rig.pull.comparison().value("summary").toString().isEmpty());
    QVERIFY(!rig.pull.limits().isEmpty());
    // compensation mode through the inspector: one undoable step
    const auto depth = rig.steps();
    QVERIFY(rig.pull.mode() >= 0);
    QVERIFY(rig.pull.setMode(1));
    QCOMPARE(rig.steps(), depth + 1);
    QCOMPARE(rig.pull.mode(), 1);
    QVERIFY(rig.session.undo());
    // calibration data round trip
    const auto rows = rig.pull.rows();
    QVERIFY(rows.size() >= 6);
    for (const auto& r : rows) {
        const auto m = r.toMap();
        QVERIFY(rig.pull.setMeasure(m.value("id").toString(), m.value("predictedMm").toDouble()));
    }
    QCOMPARE(rig.pull.measuredCount(), static_cast<int>(rows.size()));
    QVERIFY(rig.pull.fit().value("enoughData").toBool());
    QVERIFY(!rig.pull.setMeasure(QStringLiteral("inconnu"), 1.0));
    const auto csv = rig.dir.filePath("calib.csv");
    QVERIFY(rig.pull.exportCalibration(csv));
    rig.pull.clearMeasures();
    QCOMPARE(rig.pull.measuredCount(), 0);
    QVERIFY(rig.pull.importCalibration(csv));
    QCOMPARE(rig.pull.measuredCount(), static_cast<int>(rows.size()));
    QVERIFY(rig.pull.writeTestPiece(rig.dir.filePath("piece.dst")));
    QVERIFY(QFileInfo(rig.dir.filePath("piece.dst")).size() > 0);
    // colour registration, off the UI thread
    rig.pull.predictRegistration(0.0);
    QTRY_VERIFY_WITH_TIMEOUT(!rig.pull.busy(), 30'000);
    QVERIFY(!rig.pull.registration().isEmpty());
}

void StudioDigitize2Test::appliqueConvertAndCut() {
    Rig rig;
    open(rig);
    QVERIFY(rig.applique.canConvert());
    QVERIFY(!rig.applique.exportCut(rig.dir.filePath("none.svg"))); // no applique yet: refused
    const auto before = dstBytes(rig.session);
    const auto depth = rig.steps();
    QVERIFY(rig.applique.convert());
    QCOMPARE(rig.steps(), depth + 1);
    QTRY_VERIFY(rig.applique.isApplique());
    QVERIFY(dstBytes(rig.session) != before);
    QVERIFY(rig.applique.setField(QStringLiteral("cut_offset_mm"), 1.0));
    QVERIFY(rig.applique.setField(QStringLiteral("tack"), 2));
    const auto svg = rig.dir.filePath("cut.svg");
    QVERIFY(rig.applique.exportCut(svg));
    QVERIFY(QFileInfo(svg).size() > 0);
    QVERIFY(rig.session.undo());
    QVERIFY(rig.session.undo());
    QVERIFY(rig.session.undo());
    QVERIFY(dstBytes(rig.session) == before);
}

void StudioDigitize2Test::overlayIsGroupedAndNonMutating() {
    Rig rig;
    open(rig);
    rig.digitizer.setMode(QStringLiteral("points"));
    QTRY_VERIFY(rig.points.pointCount() > 100);
    const auto builds = rig.canvas->geometryBuildCount();
    const auto overlay = rig.canvas->handlesOverlay();
    // a handful of grouped items, never one per point
    QVERIFY(!overlay.isEmpty());
    QVERIFY(overlay.size() <= 4);
    rig.canvas->panBy(15, 9);
    rig.canvas->zoomAt(1.2, 100, 100);
    QVERIFY(rig.canvas->handlesOverlay().size() <= 4);
    QCOMPARE(rig.canvas->geometryBuildCount(), builds); // pan/zoom never rebuild stitch geometry
    auto* overlayItem = findVisual(rig.window->contentItem(), QStringLiteral("handlesOverlay"));
    QVERIFY(overlayItem);
    rig.digitizer.setMode(QString());
    QVERIFY(rig.canvas->handlesOverlay().isEmpty());
}

QTEST_MAIN(StudioDigitize2Test)
#include "test_studio_digitize2.moc"
