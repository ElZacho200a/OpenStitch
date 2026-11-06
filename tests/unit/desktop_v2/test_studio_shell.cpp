// SPDX-License-Identifier: Apache-2.0
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <array>
#include <cmath>
#include <filesystem>
#include <memory>

#include "gpu_canvas.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "studio_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {
constexpr std::uint64_t firstId = (1ULL << 54) + 17;
constexpr std::uint64_t secondId = (1ULL << 54) + 19;

document::Project studioFixture() {
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

QPoint windowPoint(QQuickItem& item, ScenePoint point) {
    return item.mapToScene(QPointF(point.x, point.y)).toPoint();
}
} // namespace

class StudioShellTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void realProjectSelectionAndViewInteractions();
    void softwareResourcesRecreateFromTheSameSnapshot();
    void prepopulatedSessionAndFailedGeometry();
    void controllerDisconnectionClearsTheCanvas();
};

void StudioShellTest::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                 QStringLiteral("Provided by the application"));
}

void StudioShellTest::realProjectSelectionAndViewInteractions() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = std::filesystem::path(directory.path().toStdWString()) / "studio-test.osp";
    const auto saved = project_io::save_project(path, studioFixture());
    QVERIFY2(saved.has_value(), saved ? "" : saved.error().message.c_str());
    QFile source(QString::fromStdWString(path.wstring()));
    QVERIFY(source.open(QIODevice::ReadOnly));
    const auto originalBytes = source.readAll();
    QVERIFY(!originalBytes.isEmpty());
    source.close();

    DocumentSession session;
    StudioController controller(session);
    QQmlApplicationEngine engine;
    QStringList qmlWarnings;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError>& errors) {
        for (const auto& error : errors) {
            qmlWarnings.push_back(error.toString());
        }
    });
    engine.rootContext()->setContextProperty(QStringLiteral("studio"), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(qmlWarnings.join('\n')));
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    auto* canvas = window->findChild<GpuCanvas*>(QStringLiteral("studioCanvas"));
    QVERIFY(canvas);
    auto* objectList = window->findChild<QQuickItem*>(QStringLiteral("objectList"));
    QVERIFY(objectList);
    QSignalSpy loaded(&controller, &StudioController::loadFinished);
    QSignalSpy frames(window, &QQuickWindow::frameSwapped);
    controller.openProject(QUrl::fromLocalFile(QString::fromStdWString(path.wstring())));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy() && controller.scene() != nullptr, 15'000);
    QVERIFY(!loaded.isEmpty());
    QVERIFY(loaded.last().front().toBool());
    QTRY_VERIFY_WITH_TIMEOUT(canvas->geometryBuildCount() > 0, 10'000);
    QTRY_COMPARE(objectList->property("count").toInt(), 2);
    QVERIFY(canvas->backendName().contains(QStringLiteral("CPU")));
    QCOMPARE(controller.objects().front().toMap().value(QStringLiteral("id")).toString(),
             QString::number(firstId)); // no loss through JavaScript double precision

    const auto frozen = session.snapshot();
    const auto revision = session.revision();
    const auto scene = canvas->scene();
    const auto commands = stitch_generation::effective_sequence(*frozen);
    QVERIFY(commands.has_value());
    const auto dstBefore = formats::encode_dst(*commands);
    QVERIFY(dstBefore.has_value());
    const auto builds = canvas->geometryBuildCount();
    const auto firstPoint = canvas->camera().worldToScreen({0, -10});
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, windowPoint(*canvas, firstPoint));
    QTRY_VERIFY(session.selection().contains({TargetKind::EmbroideryObject, firstId}));
    QTRY_VERIFY(controller.objects().front().toMap().value(QStringLiteral("selected")).toBool());

    QQuickItem* secondRow = nullptr;
    QTRY_VERIFY(QMetaObject::invokeMethod(objectList, "itemAtIndex",
                                          Q_RETURN_ARG(QQuickItem*, secondRow), Q_ARG(int, 1)) &&
                secondRow != nullptr);
    QTest::mouseClick(
        window, Qt::LeftButton, Qt::NoModifier,
        secondRow->mapToScene(QPointF(secondRow->width() / 2, secondRow->height() / 2)).toPoint());
    QTRY_VERIFY(session.selection().contains({TargetKind::EmbroideryObject, secondId}));
    QCOMPARE(session.selection().targets().size(), std::size_t{1});
    QVERIFY(!controller.objects().front().toMap().value(QStringLiteral("selected")).toBool());
    QVERIFY(controller.objects().back().toMap().value(QStringLiteral("selected")).toBool());

    const ScenePoint cursor{canvas->width() * 0.4, canvas->height() * 0.4};
    const auto anchor = canvas->camera().screenToWorld(cursor);
    canvas->zoomAt(1.4, cursor.x, cursor.y);
    const auto afterZoom = canvas->camera().screenToWorld(cursor);
    QVERIFY(std::abs(afterZoom.x - anchor.x) < 1e-9);
    QVERIFY(std::abs(afterZoom.y - anchor.y) < 1e-9);
    const auto previousOffset = canvas->camera().offset();
    const QPoint middle = windowPoint(*canvas, {canvas->width() / 2, canvas->height() / 2});
    QTest::mousePress(window, Qt::MiddleButton, Qt::NoModifier, middle);
    QTest::mouseMove(window, middle + QPoint(24, -13));
    QTest::mouseRelease(window, Qt::MiddleButton, Qt::NoModifier, middle + QPoint(24, -13));
    QVERIFY(std::abs(canvas->camera().offset().x - previousOffset.x - 24) < 1.1);
    QVERIFY(std::abs(canvas->camera().offset().y - previousOffset.y + 13) < 1.1);
    const auto frameCount = frames.count();
    window->update();
    QTRY_VERIFY_WITH_TIMEOUT(frames.count() > frameCount, 10'000);
    QCOMPARE(canvas->geometryBuildCount(), builds);
    QVERIFY(canvas->scene() == scene);
    QVERIFY(session.snapshot() == frozen);
    QCOMPARE(session.revision(), revision);
    QVERIFY(!session.dirty());
    QVERIFY(!session.canUndo());
    const auto after = stitch_generation::effective_sequence(session.project());
    QVERIFY(after.has_value());
    const auto dstAfter = formats::encode_dst(*after);
    QVERIFY(dstAfter.has_value());
    QVERIFY(*dstAfter == *dstBefore);

    // A failed open leaves the current document and its already-built geometry available.
    loaded.clear();
    controller.openProject(QUrl::fromLocalFile(directory.path() + QStringLiteral("/absent.osp")));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15'000);
    QVERIFY(!loaded.isEmpty());
    QVERIFY(!loaded.last().front().toBool());
    QVERIFY(controller.scene() == scene);
    QVERIFY(canvas->scene() == scene);
    QVERIFY(session.snapshot() == frozen);
    QCOMPARE(session.revision(), revision);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), originalBytes);
    source.close();
    QVERIFY2(qmlWarnings.isEmpty(), qPrintable(qmlWarnings.join('\n')));
}

void StudioShellTest::softwareResourcesRecreateFromTheSameSnapshot() {
    const auto scene = build_studio_scene(
        std::make_shared<const document::Project>(studioFixture()), 1, TaskContext{});
    QVERIFY(scene.has_value());
    // The Qt software adaptation retains its graph on hide/releaseResources. Destroy the
    // window to exercise real resource destruction/recreation without assuming a GPU signal.
    for (int lifetime = 0; lifetime < 2; ++lifetime) {
        QQuickWindow window;
        window.resize(800, 600);
        GpuCanvas canvas(window.contentItem());
        canvas.setWidth(800);
        canvas.setHeight(600);
        canvas.setScene(*scene);
        QCOMPARE(canvas.geometryBuildCount(), 0);
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(canvas.geometryBuildCount(), 1, 10'000);
        QVERIFY(canvas.scene() == *scene);
        window.resize(900, 650);
        canvas.setWidth(900);
        canvas.setHeight(650);
        canvas.panBy(20, 10);
        QSignalSpy frames(&window, &QQuickWindow::frameSwapped);
        window.update();
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 10'000);
        QCOMPARE(canvas.geometryBuildCount(), 1);
    }
}

void StudioShellTest::controllerDisconnectionClearsTheCanvas() {
    DocumentSession session;
    auto controller = std::make_unique<StudioController>(session);
    GpuCanvas canvas;
    canvas.setController(controller.get());
    session.replaceProject(studioFixture(), true);
    QTRY_VERIFY_WITH_TIMEOUT(!controller->busy() && canvas.scene() != nullptr, 15'000);
    canvas.setController(nullptr);
    QVERIFY(canvas.controller() == nullptr);
    QVERIFY(canvas.scene() == nullptr);
    canvas.setController(controller.get());
    QVERIFY(canvas.scene() != nullptr);
    controller.reset();
    QVERIFY(canvas.controller() == nullptr);
    QVERIFY(canvas.scene() == nullptr);
}

void StudioShellTest::prepopulatedSessionAndFailedGeometry() {
    DocumentSession session;
    session.replaceProject(studioFixture(), true);
    StudioController controller(session);
    GpuCanvas canvas;
    canvas.setController(&controller);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy() && canvas.scene() != nullptr, 15'000);
    QVERIFY(controller.scene() == canvas.scene());
    const auto previousScene = controller.scene();
    QSignalSpy loaded(&controller, &StudioController::loadFinished);
    auto broken = studioFixture();
    broken.vector_objects.clear(); // source geometry missing: build must fail, never show old data
    session.replaceProject(std::move(broken), true);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15'000);
    QVERIFY(!loaded.isEmpty());
    QVERIFY(!loaded.last().front().toBool());
    QVERIFY(controller.scene() == nullptr);
    QVERIFY(canvas.scene() == nullptr);
    QVERIFY(previousScene != nullptr); // old immutable snapshot can safely outlive GPU resources
}

QTEST_MAIN(StudioShellTest)
#include "test_studio_shell.moc"
