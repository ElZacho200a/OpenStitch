// SPDX-License-Identifier: Apache-2.0
#include "action_controller.hpp"
#include "edit_controller.hpp"
#include "gpu_canvas.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "tool_controller.hpp"
#include "workspace_controller.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
namespace {
std::vector<std::uint8_t> dst(const document::Project& project) {
    auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence)
        return {};
    auto bytes = formats::encode_dst(*sequence);
    return bytes ? *bytes : std::vector<std::uint8_t>{};
}
QQuickItem* find(QQuickItem* root, const QString& name) {
    if (root->objectName() == name)
        return root;
    for (auto* child : root->childItems())
        if (auto* result = find(child, name))
            return result;
    return nullptr;
}
struct Rig {
    DocumentSession session;
    StudioController studio{session};
    ActionController actions{studio};
    EditController editor{studio};
    ToolController tools{studio};
    QStringList warnings; // outlives engine and its destruction notifications
    QQmlApplicationEngine engine;
    QQuickWindow* window{};
    GpuCanvas* canvas{};
    bool open(bool walls = false) {
        auto empty = project_ops::create_empty_project({});
        if (!empty)
            return false;
        session.replaceProject(std::move(*empty), true);
        const auto batches = walls ? std::vector<std::string>{batchAddRect({-12, 0}, 4, 28),
                                                              batchAddRect({12, 0}, 4, 28),
                                                              batchAddRect({0, -12}, 28, 4),
                                                              batchAddRect({0, 12}, 28, 4)}
                                   : std::vector<std::string>{batchAddRect({0, 0}, 20, 10)};
        for (const auto& batch : batches)
            if (!session.applyBatch(batch, "Fixture").committed)
                return false;
        session.replaceProject(session.project(), true);
        studio.selectObject(QString::number(session.project().embroidery_objects.front().id.value));
        QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                         [this](const QList<QQmlError>& errors) {
                             for (const auto& error : errors)
                                 warnings.push_back(error.toString());
                         });
        engine.rootContext()->setContextProperty("studio", &studio);
        engine.rootContext()->setContextProperty("actions", &actions);
        engine.rootContext()->setContextProperty("editor", &editor);
        engine.rootContext()->setContextProperty("tools", &tools);
        engine.load(QUrl("qrc:/studio/qml/Main.qml"));
        if (engine.rootObjects().isEmpty())
            return false;
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        if (!window)
            return false;
        window->resize(1400, 1000);
        window->show();
        if (!QTest::qWaitForWindowExposed(window))
            return false;
        if (auto* workspace = window->findChild<WorkspaceController*>())
            workspace->setCurrent(QStringLiteral("vector")); // le rail d'outils vit dans cet espace
        canvas = window->findChild<GpuCanvas*>("studioCanvas");
        tools.setSnapVertices(false);
        if (!canvas || !settle())
            return false;
        canvas->fitDesign();
        return warnings.isEmpty();
    }
    bool settle() {
        return QTest::qWaitFor(
            [this] {
                return !studio.busy() && !tools.busy() && studio.scene() &&
                       studio.scene()->revision == session.revision();
            },
            30000);
    }
    QPoint at(double x, double y) {
        auto p = canvas->camera().worldToScreen({x, -y});
        return canvas->mapToScene(QPointF(p.x, p.y)).toPoint();
    }
    bool choose(const char* tool) {
        auto* button =
            find(window->contentItem(), QStringLiteral("tool_") + QString::fromLatin1(tool));
        if (!button || !button->isVisible())
            return false;
        QTest::mouseClick(
            window, Qt::LeftButton, {},
            button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        return tools.activeTool() == QString::fromLatin1(tool);
    }
};
} // namespace
class StudioWarpBucketTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        QQuickWindow::setSceneGraphBackend("software");
        qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    }
    void mouseWarpPreviewCancelUndoAndOsp() {
        Rig rig;
        QVERIFY(rig.open());
        const auto initial = dst(rig.session.project());
        QVERIFY(!initial.empty());
        const auto revision = rig.session.revision();
        const auto source = rig.session.project().embroidery_objects.front().source_vector;
        const auto paths = rig.session.project().findObject(source)->paths;
        QVERIFY(rig.choose("warp-brush"));
        QTest::mousePress(rig.window, Qt::LeftButton, {}, rig.at(0, 5));
        QTest::mouseMove(rig.window, rig.at(0, 7));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.tools.overlay().empty(), 10000);
        QCOMPARE(rig.session.revision(), revision);
        QCOMPARE(dst(rig.session.project()), initial);
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QTest::mouseRelease(rig.window, Qt::LeftButton, {}, rig.at(0, 7));
        QVERIFY(rig.settle());
        QCOMPARE(rig.session.revision(), revision);
        QVERIFY(rig.tools.overlay().empty());
        QTest::mousePress(rig.window, Qt::LeftButton, {}, rig.at(0, 5));
        QTest::mouseMove(rig.window, rig.at(0, 7));
        QTest::mouseRelease(rig.window, Qt::LeftButton, {}, rig.at(0, 7));
        QVERIFY(rig.settle());
        QCOMPARE(rig.session.undoNames().size(), std::size_t{1});
        QVERIFY(rig.session.project().findObject(source)->paths != paths);
        QTemporaryDir directory;
        auto path = std::filesystem::path(directory.filePath("warp.osp").toStdWString());
        QVERIFY(project_io::save_project(path, rig.session.project()));
        auto reopened = project_io::load_project(path);
        QVERIFY(reopened);
        QCOMPARE(reopened->findObject(source)->paths,
                 rig.session.project().findObject(source)->paths);
        QCOMPARE(dst(*reopened), dst(rig.session.project()));
        QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(rig.settle());
        QCOMPARE(dst(rig.session.project()), initial);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }
    void mouseBucketPreviewEscapeAndSingleCommit() {
        Rig rig;
        QVERIFY(rig.open(true));
        const auto initial = dst(rig.session.project());
        const auto count = rig.session.project().vector_objects.size();
        QVERIFY(rig.choose("bucket"));
        QTest::mousePress(rig.window, Qt::LeftButton, {}, rig.at(0, 0));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.tools.overlay().empty(), 10000);
        QCOMPARE(rig.session.project().vector_objects.size(), count);
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QTest::mouseRelease(rig.window, Qt::LeftButton, {}, rig.at(0, 0));
        QVERIFY(rig.settle());
        QCOMPARE(rig.session.project().vector_objects.size(), count);
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, rig.at(0, 0));
        QVERIFY(rig.settle());
        QCOMPARE(rig.session.project().vector_objects.size(), count + 1);
        QCOMPARE(rig.session.undoNames().size(), std::size_t{1});
        QCOMPARE(dst(rig.session.project()), initial); // vector-only fill, as in V1
        QTemporaryDir directory;
        auto path = std::filesystem::path(directory.filePath("bucket.osp").toStdWString());
        QVERIFY(project_io::save_project(path, rig.session.project()));
        auto reopened = project_io::load_project(path);
        QVERIFY(reopened);
        QCOMPARE(reopened->vector_objects.back().paths,
                 rig.session.project().vector_objects.back().paths);
        QCOMPARE(dst(*reopened), initial);
        QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(rig.settle());
        QCOMPARE(rig.session.project().vector_objects.size(), count);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }
};
QTEST_MAIN(StudioWarpBucketTest)
#include "test_studio_warp_bucket.moc"
