// SPDX-License-Identifier: Apache-2.0
// CX01 : vraie QML, vraie image PNG, souris sur GpuCanvas, commandes et génération DST réelles.
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <filesystem>

#include "action_controller.hpp"
#include "detail_controller.hpp"
#include "edit_controller.hpp"
#include "gpu_canvas.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "point_edit.hpp"
#include "studio_controller.hpp"
#include "tool_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {
document::Project sourceProject(const QString& path) {
    QImage png(96, 64, QImage::Format_RGB32);
    for (int y = 0; y < png.height(); ++y) {
        for (int x = 0; x < png.width(); ++x) {
            png.setPixelColor(x, y, x < 48 ? QColor(220, 30, 40) : QColor(40, 60, 200));
        }
    }
    if (!png.save(path)) {
        qFatal("Cannot write PNG fixture");
    }
    auto image = image::load_image(std::filesystem::path(path.toStdWString()));
    if (!image) {
        qFatal("Cannot load real PNG fixture");
    }
    document::Project project;
    project.original = std::move(*image);
    project.mm_per_px = Millimeters{0.5};
    project.finishing = document::SequenceFinishing::legacy();
    for (unsigned i = 0; i < 2; ++i) {
        document::VectorObject vector;
        vector.id = ObjectId{11u + i};
        geometry::Path path;
        path.closed = false;
        const auto y = Micrometers{i == 0 ? -30'000 : 30'000};
        path.nodes = {{{Micrometers{-30'000}, y}}, {{Micrometers{30'000}, y}}};
        vector.paths.push_back({path, {}});
        project.vector_objects.push_back(vector);
        document::EmbroideryObject object;
        object.id = ObjectId{101u + i};
        object.source_vector = vector.id;
        object.name = "Untouched anchor";
        object.params = document::RunningStitchParams{Micrometers{2'000}};
        project.embroidery_objects.push_back(object);
    }
    project.object_ids.reset(300);
    return project;
}
std::vector<std::uint8_t> dst(const document::Project& project) {
    const auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence) {
        qFatal("Engine failed: %s", sequence.error().message.c_str());
    }
    auto bytes = formats::encode_dst(*sequence);
    if (!bytes) {
        qFatal("DST failed: %s", bytes.error().message.c_str());
    }
    return *bytes;
}
QQuickItem* item(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) {
        return root;
    }
    for (auto* child : root->childItems()) {
        if (auto* found = item(child, name)) {
            return found;
        }
    }
    return nullptr;
}
struct Rig {
    QTemporaryDir dir;
    DocumentSession session;
    StudioController studio{session};
    ActionController actions{studio};
    EditController editor{studio};
    DetailController detail{studio};
    ToolController tools{studio};
    QQmlApplicationEngine engine;
    QStringList warnings;
    QQuickWindow* window{nullptr};
    GpuCanvas* canvas{nullptr};
};
} // namespace

class StudioDetailTest : public QObject {
    Q_OBJECT
private:
    void open(Rig& rig) {
        rig.tools.setDetailController(&rig.detail);
        connect(&rig.engine, &QQmlApplicationEngine::warnings, this,
                [&rig](const QList<QQmlError>& errors) {
                    for (const auto& error : errors) {
                        rig.warnings.push_back(error.toString());
                    }
                });
        auto* context = rig.engine.rootContext();
        context->setContextProperty(QStringLiteral("studio"), &rig.studio);
        context->setContextProperty(QStringLiteral("actions"), &rig.actions);
        context->setContextProperty(QStringLiteral("editor"), &rig.editor);
        context->setContextProperty(QStringLiteral("tools"), &rig.tools);
        context->setContextProperty(QStringLiteral("detailTools"), &rig.detail);
        rig.engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        QVERIFY2(!rig.engine.rootObjects().isEmpty(), qPrintable(rig.warnings.join('\n')));
        rig.window = qobject_cast<QQuickWindow*>(rig.engine.rootObjects().front());
        QVERIFY(rig.window);
        rig.canvas = rig.window->findChild<GpuCanvas*>(QStringLiteral("studioCanvas"));
        QVERIFY(rig.canvas);
        rig.session.replaceProject(sourceProject(rig.dir.filePath("source.png")), true);
        ready(rig);
        rig.canvas->zoomToSceneRect(-40, -35, 40, 35);
    }
    void ready(Rig& rig) {
        QTRY_VERIFY_WITH_TIMEOUT(rig.detail.ready() && !rig.detail.busy() && !rig.studio.busy() &&
                                     rig.studio.scene() &&
                                     rig.studio.scene()->revision == rig.session.revision(),
                                 20'000);
    }
    QPoint at(Rig& rig, double x, double y) {
        return rig.canvas->mapToScene(rig.canvas->sceneToCanvas(x, y)).toPoint();
    }
    void activate(Rig& rig, const char* name) {
        auto* button = item(rig.window->contentItem(), QStringLiteral("tool_") + name);
        QVERIFY(button);
        const auto pos = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, pos.toPoint());
        QCOMPARE(rig.tools.activeTool(), QString::fromLatin1(name));
    }
    void clickControl(Rig& rig, const char* name) {
        auto* button = item(rig.window->contentItem(), QString::fromLatin1(name));
        QVERIFY(button);
        QVERIFY(button->isVisible());
        const auto pos = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, pos.toPoint());
    }
private slots:
    void initTestCase() {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
        qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                     QStringLiteral("Provided by application"));
        qmlRegisterUncreatableType<EditController>("OpenStitch.Studio", 1, 0, "EditController",
                                                   QStringLiteral("Provided by application"));
        qmlRegisterUncreatableType<ToolController>("OpenStitch.Studio", 1, 0, "ToolController",
                                                   QStringLiteral("Provided by application"));
    }
    void brushErasePreviewEscapeUndoPersistAndDst() {
        Rig rig;
        open(rig);
        const auto originalDst = dst(rig.session.project());
        const auto revision = rig.session.revision();
        activate(rig, "detail-brush");
        const auto a = at(rig, 4, 5);
        const auto b = at(rig, 14, 5);
        QTest::mousePress(rig.window, Qt::LeftButton, {}, a);
        QTest::mouseMove(rig.window, b);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.detail.maskUrl().isEmpty(), 10'000);
        QCOMPARE(rig.session.revision(), revision);
        QVERIFY(rig.session.project().detail_mask.empty());
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QTest::mouseRelease(rig.window, Qt::LeftButton, {}, b);
        QCOMPARE(rig.session.revision(), revision);
        QVERIFY(rig.detail.maskUrl().isEmpty());
        QTest::mousePress(rig.window, Qt::LeftButton, {}, a);
        QTest::mouseMove(rig.window, b);
        QTest::mouseRelease(rig.window, Qt::LeftButton, {}, b);
        QCOMPARE(rig.session.undoNames().size(), std::size_t{1});
        ready(rig);
        QVERIFY(detailWeightAt(rig.session.project(), rig.session.project().original, 9, 5) > 200);
        QVERIFY(dst(rig.session.project()) == originalDst);
        const auto paintedMask = rig.session.project().detail_mask;
        const auto path = std::filesystem::path(rig.dir.filePath("detail.osp").toStdWString());
        QVERIFY(project_io::save_project(path, rig.session.project()));
        const auto loaded = project_io::load_project(path);
        QVERIFY(loaded);
        QVERIFY(loaded->detail_mask == paintedMask);
        QVERIFY(dst(*loaded) == originalDst);
        activate(rig, "detail-erase");
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 9, 5));
        QCOMPARE(rig.session.undoNames().size(), std::size_t{2});
        ready(rig);
        QCOMPARE(detailWeightAt(rig.session.project(), rig.session.project().original, 9, 5), 0);
        QVERIFY(rig.session.undo());
        QVERIFY(rig.session.project().detail_mask == paintedMask);
        ready(rig);
        clickControl(rig, "detailClearMask");
        QVERIFY(rig.session.project().detail_mask.empty());
        QVERIFY(rig.session.undo());
        QVERIFY(rig.session.project().detail_mask == paintedMask);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }
    void clickCreatesDeterministicObjectAndPreservesOthers() {
        Rig rig;
        open(rig);
        const auto before = rig.session.snapshot();
        const auto originalDst = dst(*before);
        activate(rig, "click-to-stitch");
        rig.detail.setStitchKind(1);
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 12, 0));
        QTRY_COMPARE_WITH_TIMEOUT(rig.session.project().embroidery_objects.size(), std::size_t{3},
                                  20'000);
        ready(rig);
        QCOMPARE(rig.session.undoNames().size(), std::size_t{1});
        const auto clicked = rig.session.snapshot();
        const auto clickedDst = dst(*clicked);
        QVERIFY(clickedDst != originalDst);
        QVERIFY(clicked->findEmbroidery(ObjectId{101}));
        QVERIFY(clicked->findEmbroidery(ObjectId{102}));
        for (const auto id : {ObjectId{101}, ObjectId{102}}) {
            const auto originalView = stitch_generation::edit_view(*before, id);
            const auto clickedView = stitch_generation::edit_view(*clicked, id);
            QVERIFY(originalView && clickedView);
            QCOMPARE(originalView->fingerprint, clickedView->fingerprint);
        }
        DocumentSession reference;
        reference.replaceProject(*before, true);
        // Même coordonnées effectivement reçues (arrondi pixel QTest) et même service pur.
        const auto local = rig.canvas->mapFromScene(at(rig, 12, 0));
        const auto world = rig.canvas->camera().screenToWorld({local.x(), local.y()});
        const auto result =
            clickToStitch(reference, before->original, world.x, world.y, 20, ClickKind::Fill);
        QVERIFY2(result.ok, result.message.c_str());
        QVERIFY(dst(reference.project()) == clickedDst);
        const auto path = std::filesystem::path(rig.dir.filePath("click.osp").toStdWString());
        QVERIFY(project_io::save_project(path, *clicked));
        const auto loaded = project_io::load_project(path);
        QVERIFY(loaded);
        QVERIFY(dst(*loaded) == clickedDst);
        QVERIFY(rig.session.undo());
        QVERIFY(dst(rig.session.project()) == originalDst);
        QVERIFY(rig.session.redo());
        QVERIFY(dst(rig.session.project()) == clickedDst);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }
    void regenerationRequiresExplicitRetouchConfirmation() {
        Rig rig;
        open(rig);
        activate(rig, "click-to-stitch");
        rig.detail.setStitchKind(1);
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 12, 0));
        QTRY_COMPARE_WITH_TIMEOUT(rig.session.project().embroidery_objects.size(), std::size_t{3},
                                  20'000);
        ready(rig);
        const auto selected = rig.session.selection().targets().front().id;
        const auto view = stitchPoints(rig.session.project(), selected);
        QVERIFY(view);
        const auto movable =
            std::ranges::find_if(view->points, [](const auto& p) { return p.movable; });
        QVERIFY(movable != view->points.end());
        QVERIFY(setStitchPointsTrim(rig.session, selected, {movable->index}, true).ok);
        ready(rig);
        const auto retouchedDst = dst(rig.session.project());
        const auto beforeSteps = rig.session.undoNames().size();
        clickControl(rig, "detailReplaceSelected");
        QVERIFY(rig.detail.replaceSelected());
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, -12, 0));
        QTRY_VERIFY(rig.detail.confirmationPending());
        QCOMPARE(rig.session.undoNames().size(), beforeSteps);
        QVERIFY(dst(rig.session.project()) == retouchedDst);
        clickControl(rig, "detailCancelReplace");
        QVERIFY(!rig.detail.confirmationPending());
        QVERIFY(dst(rig.session.project()) == retouchedDst);
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, -12, 0));
        QTRY_VERIFY(rig.detail.confirmationPending());
        clickControl(rig, "detailConfirmReplace");
        QTRY_VERIFY_WITH_TIMEOUT(!rig.session.project().findEmbroidery(ObjectId{selected}), 20'000);
        ready(rig);
        QCOMPARE(rig.session.undoNames().size(), beforeSteps + 1);
        QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{3});
        QVERIFY(rig.session.project().findEmbroidery(ObjectId{101}));
        QVERIFY(rig.session.project().findEmbroidery(ObjectId{102}));
        QVERIFY(rig.session.undo());
        QVERIFY(dst(rig.session.project()) == retouchedDst);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }
    void invalidOptionsAndMissingImageDoNotMutate() {
        DocumentSession session;
        StudioController studio(session);
        DetailController detail(studio);
        const auto revision = session.revision();
        detail.setRadiusMm(-1);
        QCOMPARE(detail.radiusMm(), 2.0);
        detail.setTolerance(101);
        QCOMPARE(detail.tolerance(), 20);
        detail.setStitchKind(9);
        QCOMPARE(detail.stitchKind(), 0);
        QVERIFY(!detail.press(QStringLiteral("detail-brush"), 0, 0));
        detail.clearMask();
        QCOMPARE(session.revision(), revision);
        QVERIFY(!detail.message().isEmpty());
    }
};

QTEST_MAIN(StudioDetailTest)
#include "test_studio_detail.moc"
