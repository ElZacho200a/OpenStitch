// SPDX-License-Identifier: Apache-2.0
// CX10: real CSV/configuration, real QML events, real color engine and immutable previews.
#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>
#include <limits>

#include "action_controller.hpp"
#include "edit_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "shape_batch.hpp"
#include "thread_inventory_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
std::vector<std::uint8_t> dst(const document::Project& project) {
    auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence)
        return {};
    auto result = formats::encode_dst(*sequence);
    return result ? *result : std::vector<std::uint8_t>{};
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
    QTemporaryDir dir;
    DocumentSession session;
    StudioController studio{session};
    ActionController actions{studio};
    EditController editor{studio};
    FileController files{studio, &actions, dir.path()};
    ThreadController threads{studio, std::filesystem::path(dir.path().toStdWString())};
    ThreadInventoryController inventory{studio, threads,
                                        std::filesystem::path(dir.path().toStdWString())};
    QStringList warnings;
    QQmlApplicationEngine engine;
    QQuickWindow* window{};

    bool fixture() {
        auto created = project_ops::create_empty_project({});
        if (!created)
            return false;
        DocumentSession builder;
        builder.replaceProject(std::move(*created), true);
        if (!builder.applyBatch(batchAddRect({-15, 0}, 12, 8), "Fixture left").committed ||
            !builder.applyBatch(batchAddRect({15, 0}, 12, 8), "Fixture right").committed)
            return false;
        auto project = builder.project();
        project.embroidery_objects[0].rgb = {200, 20, 60};
        project.embroidery_objects[1].rgb = {20, 160, 80};
        session.replaceProject(std::move(project), true);
        return settle();
    }
    bool settle() {
        return QTest::qWaitFor([this] { return !studio.busy() && !inventory.busy(); }, 30000);
    }
    bool open() {
        QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                         [&](const QList<QQmlError>& errors) {
                             for (const auto& error : errors)
                                 warnings.push_back(error.toString());
                         });
        const std::vector<std::pair<const char*, QObject*>> contexts{
            {"studio", &studio}, {"actions", &actions}, {"editor", &editor},
            {"files", &files},   {"threads", &threads}, {"threadInventory", &inventory}};
        for (const auto& [name, object] : contexts)
            engine.rootContext()->setContextProperty(QString::fromLatin1(name), object);
        engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        if (engine.rootObjects().isEmpty())
            return false;
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        if (!window)
            return false;
        window->resize(1450, 1100);
        window->show();
        return QTest::qWaitForWindowExposed(window) && warnings.isEmpty();
    }
    QQuickItem* item(const char* name) {
        return find(window->contentItem(), QString::fromLatin1(name));
    }
    bool click(const char* name) {
        auto* control = item(name);
        if (!control || !control->isVisible() || !control->isEnabled())
            return false;
        for (auto* parent = control->parentItem(); parent; parent = parent->parentItem()) {
            if (!parent->property("contentY").isValid())
                continue;
            const double y = control->mapToItem(parent, QPointF(0, 0)).y();
            const double delta =
                y < 0 ? y : std::max(0.0, y + control->height() - parent->height());
            parent->setProperty("contentY", parent->property("contentY").toDouble() + delta);
        }
        QCoreApplication::processEvents();
        const auto point =
            control->mapToScene(QPointF(control->width() / 2, control->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, {}, point.toPoint());
        QCoreApplication::processEvents();
        return true;
    }
    bool type(const char* name, const QString& value) {
        if (!click(name))
            return false;
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QInputMethodEvent input;
        input.setCommitString(value);
        auto* focus = window->activeFocusItem();
        if (!focus)
            return false;
        QCoreApplication::sendEvent(focus, &input);
        QTest::keyClick(window, Qt::Key_Return);
        return true;
    }
};
} // namespace

class StudioThreads2Test : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    }
    void profileSurvivesRestartAndCorruptionDoesNotOverwriteIt() {
        Rig rig;
        QVERIFY(rig.fixture());
        const auto before = dst(rig.session.project());
        QVERIFY(!before.empty());
        const auto revision = rig.session.revision();
        QVERIFY(rig.inventory.addPersonal("P-01", "My burgundy", "#aa2244", "Atelier"));
        QVERIFY(rig.inventory.owns("personal", "P-01"));
        const auto file = rig.dir.filePath("thread-stock.json");
        const auto saved = read(file);
        QVERIFY(!saved.isEmpty());
        ThreadInventoryController reopened(rig.studio, rig.threads,
                                           std::filesystem::path(rig.dir.path().toStdWString()));
        QCOMPARE(reopened.stock().size(), 1);
        QCOMPARE(reopened.stock().front().toMap().value("name").toString(),
                 QStringLiteral("My burgundy"));
        QVERIFY(!reopened.addPersonal("", "Invalid", "#ffffff"));
        QVERIFY(!reopened.addPersonal("P-02", "Invalid", "#12ggff"));
        QCOMPARE(read(file), saved);
        QVERIFY(write(file, "{malformed profile"));
        QVERIFY(!reopened.reload());
        QVERIFY(!reopened.addPersonal("P-02", "Must not replace invalid file", "#ffffff"));
        QCOMPARE(read(file), QByteArray("{malformed profile"));
        QVERIFY(write(file, saved));
        QVERIFY(reopened.reload());
        const auto loaded = loadThreadStock(std::filesystem::path(file.toStdWString()));
        QVERIFY(loaded.has_value());
        QVERIFY(saveThreadStock(std::filesystem::path(file.toStdWString()), *loaded));
        QCOMPARE(read(file), saved); // deterministic bytes, also exercises atomic replacement
        QCOMPARE(rig.session.revision(), revision);
        QCOMPARE(dst(rig.session.project()), before);
        QVERIFY(!rig.session.dirty());
    }

    void colorComparisonUsesReferenceValuesAndRejectsInvalidRatios() {
        Rig rig;
        QVERIFY(rig.fixture());
        const auto before = dst(rig.session.project());
        const auto compared = rig.inventory.compareColors("#000000", "#ffffff", 0.5);
        QVERIFY(std::abs(compared.value("difference").toDouble() - 100) < 0.001);
        QCOMPARE(compared.value("mixed").value<QColor>(), QColor(188, 188, 188));
        QCOMPARE(
            rig.inventory.compareColors("#aa2244", "#aa2244", 0.3).value("difference").toDouble(),
            0.0);
        const auto reference = thread_palette::ciede2000({50, 2.6772, -79.7751}, {50, 0, -82.7485});
        QVERIFY(std::abs(reference - 2.0425) <
                0.0001); // published Sharma reference, same as palette suite
        QVERIFY(rig.inventory.compareColors("#000000", "#ffffff", -0.1).isEmpty());
        QVERIFY(rig.inventory
                    .compareColors("#000000", "#ffffff", std::numeric_limits<double>::quiet_NaN())
                    .isEmpty());
        QVERIFY(rig.inventory.compareColors("transparent", "#ffffff", 0.5).isEmpty());
        QCOMPARE(dst(rig.session.project()), before);
    }

    void realQmlOwnsThreadsImportsChartsAndAddsPersonalStock() {
        Rig rig;
        QVERIFY(rig.fixture());
        const auto before = dst(rig.session.project());
        const auto revision = rig.session.revision();
        QVERIFY(rig.open());
        QVERIFY(rig.click("threadInventoryToggle"));
        auto* dialog = rig.window->findChild<QObject*>("threadInventoryDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(rig.type("personalThreadCode", "P-UI"));
        QVERIFY(rig.type("personalThreadName", "Burgundy bobbin"));
        QVERIFY(rig.type("personalThreadColor", "#bb3366"));
        QVERIFY(rig.click("personalThreadAdd"));
        QTRY_COMPARE(rig.inventory.stock().size(), 1);
        const auto csv = rig.dir.filePath("cx10_inventory.csv");
        QVERIFY(write(csv, "code,name,hex\nA,Red,#c8102e\nB,Blue,#0033cc\n"));
        QVERIFY(rig.inventory.importChart(QUrl::fromLocalFile(csv)));
        int selected = -1;
        QString chart;
        const auto charts = rig.inventory.charts();
        for (int index = 0; index < charts.size(); ++index) {
            const auto row = charts[index].toMap();
            if (row.value("user").toBool() && row.value("id").toString().contains("cx10")) {
                selected = index;
                chart = row.value("id").toString();
            }
        }
        QVERIFY(selected >= 0);
        rig.item("inventoryChart")->setProperty("currentIndex", selected);
        QVERIFY(rig.click("inventorySearch"));
        QTRY_VERIFY(rig.item("ownThread_A"));
        QVERIFY(rig.click("ownThread_A"));
        QTRY_VERIFY(rig.inventory.owns(chart, "A"));
        QCOMPARE(rig.inventory.stock().size(), 2);
        QVERIFY(rig.click("inventoryCompare"));
        QVERIFY(rig.item("inventoryDeltaE")->property("text").toString().contains("100.000"));
        QVERIFY(rig.click("inventorySuggest"));
        QVERIFY(rig.inventory.messageOk());
        const auto bad = rig.dir.filePath("cx10_invalid.csv");
        QVERIFY(write(bad, "code,name,hex\n,Missing code,#ffffff\n"));
        QVERIFY(!rig.inventory.importChart(QUrl::fromLocalFile(bad)));
        QCOMPARE(rig.inventory.stock().size(), 2);
        QVERIFY(rig.click("inventoryRemoveChart"));
        QVERIFY(rig.inventory.search(chart, {}).isEmpty());
        QVERIFY(
            rig.inventory.owns(chart, "A")); // removing a chart does not silently discard ownership
        QCOMPARE(rig.session.revision(), revision);
        QCOMPARE(dst(rig.session.project()), before);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void variantPreviewIsImmutableAndActivationIsOneUndoStep() {
        Rig rig;
        QVERIFY(rig.fixture());
        const auto original = rig.session.project().embroidery_objects.front().rgb;
        QVERIFY(rig.threads.addColorway("Alternate"));
        QVERIFY(rig.threads.recolorThread(0, "#2244bb"));
        QVERIFY(rig.settle());
        const auto alternate = rig.session.project().embroidery_objects.front().rgb;
        const auto before = dst(rig.session.project());
        const auto revision = rig.session.revision();
        const auto depth = rig.session.undoNames().size();
        QVERIFY(rig.open());
        QVERIFY(rig.click("threadInventoryToggle"));
        auto* dialog = rig.window->findChild<QObject*>("threadInventoryDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(rig.click("inventoryPreview"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.inventory.previewUrl().isEmpty(), 30000);
        QCOMPARE(rig.inventory.previewIndex(), 0);
        QCOMPARE(rig.session.revision(), revision);
        QCOMPARE(rig.session.project().embroidery_objects.front().rgb, alternate);
        QCOMPARE(dst(rig.session.project()), before);
        const auto png = QByteArray::fromBase64(
            rig.inventory.previewUrl().toString().split(',').last().toLatin1());
        QVERIFY(png.startsWith(QByteArray::fromHex("89504e470d0a1a0a")));
        QVERIFY(rig.click("inventoryActivate"));
        QCOMPARE(rig.session.undoNames().size(), depth + 1);
        QCOMPARE(rig.session.project().embroidery_objects.front().rgb, original);
        QVERIFY(rig.inventory.previewUrl().isEmpty());
        const auto path = std::filesystem::path(rig.dir.filePath("colorway.osp").toStdWString());
        QVERIFY(project_io::save_project(path, rig.session.project()));
        const auto loaded = project_io::load_project(path);
        QVERIFY(loaded);
        QCOMPARE(loaded->embroidery_objects.front().rgb, original);
        QCOMPARE(dst(*loaded), dst(rig.session.project()));
        QVERIFY(rig.session.undo());
        QVERIFY(rig.settle());
        QCOMPARE(rig.session.project().embroidery_objects.front().rgb, alternate);
        QCOMPARE(dst(rig.session.project()), before);
        // An edit invalidates both a completed preview and an in-flight result.
        rig.inventory.previewColorway(0);
        rig.studio.selectObject(
            QString::number(rig.session.project().embroidery_objects.front().id.value));
        QVERIFY(rig.studio.moveSelection(1, 0));
        QVERIFY(rig.settle());
        QVERIFY(rig.inventory.previewUrl().isEmpty());
        QVERIFY(!rig.inventory.activatePreview());
        rig.inventory.previewColorway(0);
        rig.inventory.clearPreview();
        QVERIFY(rig.settle());
        QVERIFY(rig.inventory.previewUrl().isEmpty());
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }
};

QTEST_MAIN(StudioThreads2Test)
#include "test_studio_threads2.moc"
