// SPDX-License-Identifier: Apache-2.0
#include "action_controller.hpp"
#include "analysis_controller.hpp"
#include "compare_controller.hpp"
#include "digitize_pipeline.hpp"
#include "edit_controller.hpp"
#include "file_controller.hpp"
#include "inspector_controller.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "thread_controller.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <cmath>
#include <limits>
using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
namespace {
document::Project fixture(const QTemporaryDir& dir) {
    QImage image(80, 60, QImage::Format_RGBA8888);
    image.fill(QColor(220, 40, 70));
    for (int y = 0; y < 60; ++y)
        for (int x = 40; x < 80; ++x)
            image.setPixelColor(x, y, QColor(25, 160, 100));
    const auto path = dir.filePath("blocks.png");
    if (!image.save(path))
        qFatal("fixture write failed");
    DigitizeRequest request;
    request.image = std::filesystem::path(path.toStdWString());
    request.settings.max_colors = 2;
    request.settings.min_region_px = 8;
    auto result = run_digitize(request, TaskContext{});
    if (!result)
        qFatal("real digitizer failed");
    return std::move(result->project);
}
std::vector<std::uint8_t> dst(const document::Project& project) {
    const auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence)
        qFatal("effective sequence failed");
    const auto bytes = formats::encode_dst(*sequence);
    if (!bytes)
        qFatal("DST encode failed");
    return *bytes;
}
QQuickItem* named(QQuickItem* root, const QString& name) {
    if (root->objectName() == name)
        return root;
    for (auto* child : root->childItems())
        if (auto* found = named(child, name))
            return found;
    return nullptr;
}
bool press(QQuickWindow* window, const char* name) {
    auto* item = named(window->contentItem(), QString::fromLatin1(name));
    if (!item || !item->isVisible() || !item->isEnabled())
        return false;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    return true;
}
} // namespace
class StudioCompareTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (qEnvironmentVariable("QSG_RHI_BACKEND") == QStringLiteral("d3d11"))
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
        qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                     QStringLiteral("session"));
    }
    void realQmlLayersCurtainAndPreview() {
        QTemporaryDir dir;
        DocumentSession session;
        StudioController studio(session);
        ActionController actions(studio);
        EditController editor(studio);
        FileController files(studio, &actions, dir.path());
        InspectorController inspector(studio);
        ViewController view(studio);
        ThreadController threads(studio);
        AnalysisController analysis(studio, view);
        ImageController images(studio);
        CompareController compare(studio, view, images);
        QStringList warnings;
        QQmlApplicationEngine engine;
        connect(&engine, &QQmlApplicationEngine::warnings, this,
                [&](const QList<QQmlError>& errors) {
                    for (const auto& e : errors)
                        warnings.push_back(e.toString());
                });
        auto* ctx = engine.rootContext();
        ctx->setContextProperty("studio", &studio);
        ctx->setContextProperty("actions", &actions);
        ctx->setContextProperty("editor", &editor);
        ctx->setContextProperty("files", &files);
        ctx->setContextProperty("inspector", &inspector);
        ctx->setContextProperty("view", &view);
        ctx->setContextProperty("threads", &threads);
        ctx->setContextProperty("analysis", &analysis);
        ctx->setContextProperty("compare", &compare);
        engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        QVERIFY(window);
        window->hide();
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        session.replaceProject(fixture(dir), true);
        QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && images.imageWidth() > 0 && compare.hasSource() &&
                                     compare.hasSegmentation() && view.simReady(),
                                 20000);
        const auto before = dst(session.project());
        const auto revision = session.revision();
        auto* canvas = window->findChild<GpuCanvas*>("studioCanvas");
        QVERIFY(canvas);
        QVERIFY(press(window, "compareToggle"));
        QVERIFY(press(window, "compareSource"));
        QVERIFY(compare.sourceVisible());
        QVERIFY(press(window, "compareSegmentation"));
        QVERIFY(compare.segmentationVisible());
        QVERIFY(press(window, "compareVectors"));
        QVERIFY(compare.vectorsVisible());
        QVERIFY(!compare.layers()->vectors.empty());
        QVERIFY(press(window, "compareOrder"));
        QVERIFY(compare.orderVisible());
        QVERIFY(press(window, "compareCurtain"));
        QVERIFY(compare.curtain());
        auto* slider = named(window->contentItem(), QStringLiteral("comparePosition"));
        QVERIFY(slider);
        slider->forceActiveFocus();
        const double previous = compare.position();
        QTest::keyClick(window, Qt::Key_Right);
        QVERIFY(compare.position() > previous);
        compare.setPosition(0.25);
        const auto& source = compare.layers()->source;
        QCOMPARE(source.widthMm, images.imageWidth() * session.project().mm_per_px.value);
        QCOMPARE(source.heightMm, images.imageHeight() * session.project().mm_per_px.value);
        const auto clip =
            comparisonSourceClip(*compare.layers(), canvas->overlays()->comparisonDisplay);
        QCOMPARE(clip.left(), -source.widthMm / 2);
        QCOMPARE(clip.right(), -source.widthMm / 4);
        const auto point = canvas->camera().worldToScreen({clip.right(), 0});
        const auto restored = canvas->camera().screenToWorld(point);
        QVERIFY(std::abs(restored.x - clip.right()) < 1e-9);
        const auto data = compare.layers();
        const auto builds = compare.buildCount();
        QTRY_VERIFY_WITH_TIMEOUT(canvas->geometryBuildCount() > 0, 10000);
        if (qEnvironmentVariable("QSG_RHI_BACKEND") == QStringLiteral("d3d11"))
            QCOMPARE(window->rendererInterface()->graphicsApi(), QSGRendererInterface::Direct3D11);
        const auto geometry = canvas->geometryBuildCount();
        QSignalSpy frames(window, &QQuickWindow::frameSwapped);
        canvas->panBy(17, -8);
        canvas->zoomAt(1.25, 200, 120);
        compare.setOpacity(0.4);
        compare.setPosition(0.8);
        window->update();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > 0, 10000);
        QVERIFY(compare.layers() == data);
        QCOMPARE(compare.buildCount(), builds);
        QCOMPARE(canvas->geometryBuildCount(), geometry);
        QCOMPARE(session.revision(), revision);
        QVERIFY(!session.dirty());
        QVERIFY(dst(session.project()) == before);
        images.previewSegmentation({{"max_colors", 2}, {"min_region_px", 8}});
        QTRY_VERIFY_WITH_TIMEOUT(images.segPreviewActive() && compare.buildCount() > builds, 20000);
        QVERIFY(compare.hasSegmentation());
        QCOMPARE(session.revision(), revision);
        QVERIFY(dst(session.project()) == before);
        images.cancelSegmentationPreview();
        QTRY_VERIFY(!images.segPreviewActive());
        compare.setOpacity(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(compare.opacity(), 0.4);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};
QTEST_MAIN(StudioCompareTest)
#include "test_studio_compare.moc"
