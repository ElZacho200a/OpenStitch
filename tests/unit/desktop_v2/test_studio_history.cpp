// SPDX-License-Identifier: Apache-2.0
// CX11: real commands, generated stitches/DST, persisted OSP and the actual history QML.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <filesystem>

#include "action_controller.hpp"
#include "analysis_controller.hpp"
#include "edit_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "history_controller.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "shape_batch.hpp"
#include "thread_controller.hpp"
#include "view_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {
std::vector<std::uint8_t> dst(const DocumentSession& session) {
    const auto sequence = stitch_generation::effective_sequence(session.project());
    if (!sequence) {
        return {};
    }
    const auto encoded = formats::encode_dst(*sequence);
    return encoded ? *encoded : std::vector<std::uint8_t>{};
}

QQuickItem* visual(QQuickItem* item, const QString& name) {
    if (item->objectName() == name) {
        return item;
    }
    for (auto* child : item->childItems()) {
        if (auto* result = visual(child, name)) {
            return result;
        }
    }
    return nullptr;
}

struct Rig {
    DocumentSession session;
    StudioController studio{session};
    HistoryController history{studio};
    ActionController actions{studio};
    EditController editor{studio};
    FileController files{studio, &actions};
    ViewController view{studio, {}};
    ThreadController threads{studio, {}};
    AnalysisController analysis{studio, view, {}};

    void fixture() {
        auto project = project_ops::create_empty_project({});
        QVERIFY(project.has_value());
        session.replaceProject(std::move(*project), true);
        QVERIFY(session.applyBatch(batchAddRect({-15, 0}, 12, 8), "Left rectangle").committed);
        QVERIFY(session.applyBatch(batchAddRect({15, 0}, 12, 8), "Right rectangle").committed);
        auto base = session.project();
        session.replaceProject(std::move(base), true);
        QVERIFY(!dst(session).empty());
        studio.selectObject(QString::number(session.project().embroidery_objects.front().id.value));
    }
};
} // namespace

class StudioHistoryTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    }

    void jumpRestoresMachineBytesAndSavedStateThroughQml() {
        Rig rig;
        rig.fixture();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto original = dst(rig.session);
        QVERIFY(rig.studio.moveSelection(2, 0));
        const auto saved = dst(rig.session);
        QVERIFY(saved != original);
        const auto path = directory.filePath(QStringLiteral("history.osp"));
        QVERIFY(rig.studio.saveProjectAs(QUrl::fromLocalFile(path)));
        QCOMPARE(rig.history.savedIndex(), 1);
        QVERIFY(!rig.history.dirty());
        QVERIFY(rig.studio.moveSelection(0, 3));
        const auto second = dst(rig.session);
        QVERIFY(second != saved);
        QVERIFY(rig.editor.deleteSelection());
        const auto deleted = dst(rig.session);
        QCOMPARE(rig.history.currentIndex(), 3);

        QStringList warnings;
        QQmlApplicationEngine engine;
        connect(&engine, &QQmlApplicationEngine::warnings, this,
                [&warnings](const QList<QQmlError>& errors) {
                    for (const auto& error : errors) {
                        warnings.push_back(error.toString());
                    }
                });
        engine.rootContext()->setContextProperty(QStringLiteral("studio"), &rig.studio);
        engine.rootContext()->setContextProperty(QStringLiteral("history"), &rig.history);
        engine.rootContext()->setContextProperty(QStringLiteral("actions"), &rig.actions);
        engine.rootContext()->setContextProperty(QStringLiteral("editor"), &rig.editor);
        engine.rootContext()->setContextProperty(QStringLiteral("files"), &rig.files);
        engine.rootContext()->setContextProperty(QStringLiteral("view"), &rig.view);
        engine.rootContext()->setContextProperty(QStringLiteral("threads"), &rig.threads);
        engine.rootContext()->setContextProperty(QStringLiteral("analysis"), &rig.analysis);
        engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* toggle = visual(window->contentItem(), QStringLiteral("historyToggle"));
        QVERIFY(toggle);
        auto* viewToolbar = visual(window->contentItem(), QStringLiteral("viewToolbar"));
        QVERIFY(viewToolbar);
        QTRY_VERIFY(viewToolbar->width() > 0);
        QVERIFY(!toggle->mapRectToScene(toggle->boundingRect())
                     .intersects(viewToolbar->mapRectToScene(viewToolbar->boundingRect())));
        QTest::mouseClick(
            window, Qt::LeftButton, {},
            toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint());
        QTRY_VERIFY(visual(window->contentItem(), QStringLiteral("historyStep_0")) &&
                    visual(window->contentItem(), QStringLiteral("historyStep_0"))->isVisible());
        const auto clickStep = [window](int index) {
            auto* step = visual(window->contentItem(), QStringLiteral("historyStep_%1").arg(index));
            if (!step || !step->isVisible())
                return false;
            QTest::mouseClick(
                window, Qt::LeftButton, {},
                step->mapToScene(QPointF(step->width() / 2, step->height() / 2)).toPoint());
            return true;
        };
        const auto beforeJump = rig.session.revision();
        QVERIFY(clickStep(0));
        QCOMPARE(dst(rig.session), original);
        QVERIFY(rig.history.dirty());
        QVERIFY(rig.session.revision() > beforeJump);
        QVERIFY(clickStep(1));
        QCOMPARE(dst(rig.session), saved);
        QVERIFY(!rig.history.dirty());
        QVERIFY(rig.history.entries()[1].toMap()[QStringLiteral("saved")].toBool());
        QVERIFY(clickStep(2));
        QCOMPARE(dst(rig.session), second);
        QVERIFY(clickStep(3));
        QCOMPARE(dst(rig.session), deleted);
        QVERIFY(rig.session.selection().targets().empty());
        auto loaded = project_io::load_project(std::filesystem::path(path.toStdWString()));
        QVERIFY(loaded.has_value());
        rig.session.replaceProject(std::move(*loaded), true);
        QCOMPARE(dst(rig.session), saved);
        QCOMPARE(rig.history.currentIndex(), 0);
        QCOMPARE(rig.history.entries().size(), 1);
        QVERIFY(!rig.history.dirty());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void invalidAndUnchangedJumpsPreserveRevisionAndHistory() {
        Rig rig;
        rig.fixture();
        QVERIFY(rig.studio.moveSelection(2, 0));
        QVERIFY(rig.studio.moveSelection(0, 3));
        const auto sequence = dst(rig.session);
        const auto revision = rig.session.revision();
        QSignalSpy changes(&rig.history, &HistoryController::changed);
        QVERIFY(!rig.history.jumpTo(-1));
        QVERIFY(!rig.history.jumpTo(3));
        QVERIFY(rig.history.jumpTo(2));
        QCOMPARE(rig.session.revision(), revision);
        QCOMPARE(dst(rig.session), sequence);
        QCOMPARE(changes.size(), 0);
        QVERIFY(rig.history.jumpTo(0));
        QCOMPARE(changes.size(), 1); // one final-state notification, no intermediate rebuilds
        QCOMPARE(rig.session.revision(), revision + 1);
        QCOMPARE(rig.history.entries().size(), 3);
        QCOMPARE(rig.history.entries()[1].toMap()[QStringLiteral("future")].toBool(), true);
    }

    void divergentEditsInvalidateSavedMarkerAndSelectionIsPruned() {
        Rig rig;
        rig.fixture();
        QVERIFY(rig.studio.moveSelection(1, 0));
        rig.session.markSaved();
        QCOMPARE(rig.history.savedIndex(), 1);
        QVERIFY(rig.history.jumpTo(0));
        QVERIFY(rig.studio.moveSelection(0, 1));
        QCOMPARE(rig.history.savedIndex(), -1);
        QVERIFY(rig.history.dirty());
        QVERIFY(!rig.session.canRedo());
        QVERIFY(rig.session.applyBatch(batchAddRect({0, 20}, 5, 5), "New rectangle").committed);
        rig.studio.selectObject(
            QString::number(rig.session.project().embroidery_objects.back().id.value));
        QVERIFY(!rig.session.selection().targets().empty());
        QVERIFY(rig.history.jumpTo(0));
        QVERIFY(rig.session.selection().targets().empty());
        QVERIFY(rig.history.jumpTo(2));
        QVERIFY(rig.session.selection().targets().empty());
        rig.session.markSaved();
        QVERIFY(!rig.history.dirty());
        QCOMPARE(rig.history.savedIndex(), 2);
    }

    void coalescedParameterEditsStayOneLogicalHistoryStep() {
        Rig rig;
        rig.fixture();
        const auto original = dst(rig.session);
        const auto object = rig.session.project().embroidery_objects.front();
        auto params = object.params;
        auto* fill = std::get_if<document::TatamiParams>(&params);
        QVERIFY(fill);
        fill->row_spacing = Micrometers{500};
        rig.session.execute(
            std::make_unique<commands::SetStitchParamsCommand>(object.id, params, "row spacing"));
        fill->row_spacing = Micrometers{650};
        rig.session.execute(
            std::make_unique<commands::SetStitchParamsCommand>(object.id, params, "row spacing"));
        QCOMPARE(rig.history.currentIndex(), 1);
        QCOMPARE(rig.history.entries().size(), 2);
        const auto modified = dst(rig.session);
        QVERIFY(modified != original);
        QVERIFY(rig.history.jumpTo(0));
        QCOMPARE(dst(rig.session), original);
        QVERIFY(rig.history.jumpTo(1));
        QCOMPARE(dst(rig.session), modified);
        rig.session.markSaved();
        fill->row_spacing = Micrometers{800};
        rig.session.execute(
            std::make_unique<commands::SetStitchParamsCommand>(object.id, params, "row spacing"));
        QCOMPARE(rig.history.currentIndex(), 2);
        QCOMPARE(rig.history.savedIndex(), 1);
    }
};

QTEST_MAIN(StudioHistoryTest)
#include "test_studio_history.moc"
