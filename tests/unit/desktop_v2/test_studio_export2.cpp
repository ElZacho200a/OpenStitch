// SPDX-License-Identifier: Apache-2.0
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "action_controller.hpp"
#include "edit_controller.hpp"
#include "export_controller.hpp"
#include "export_settings.hpp"
#include "gpu_canvas.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/applique_cut.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {
document::Project fixture(bool applique = false) {
    document::Project p;
    p.finishing = document::SequenceFinishing::legacy();
    for (int i = 0; i < 2; ++i) {
        document::VectorObject vector;
        vector.id = p.object_ids.next();
        geometry::Path path;
        path.closed = true;
        const int x = i * 12'000 - 12'000;
        for (const auto [dx, dy] : {std::pair{0, 0}, {10'000, 0}, {10'000, 10'000}, {0, 10'000}}) {
            path.nodes.push_back({{Micrometers{x + dx}, Micrometers{dy - 5'000}}});
        }
        vector.paths.push_back({path, {}});
        p.vector_objects.push_back(vector);
        document::EmbroideryObject object;
        object.id = p.object_ids.next();
        object.source_vector = vector.id;
        object.rgb = i == 0 ? std::array<std::uint8_t, 3>{180, 17, 65}
                            : std::array<std::uint8_t, 3>{21, 180, 151};
        if (applique)
            object.params = document::AppliqueParams{};
        else
            object.params = document::TatamiParams{};
        p.embroidery_objects.push_back(object);
    }
    return p;
}
QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
std::filesystem::path pathOf(const QString& path) {
    return std::filesystem::path(path.toStdWString());
}
struct Rig {
    DocumentSession session;
    StudioController studio{session};
    ActionController actions{studio};
    EditController editor{studio};
    FileController files{studio, &actions};
    ExportController exports{studio, files};
    bool idle() {
        return QTest::qWaitFor([this] { return !exports.busy(); }, 30'000);
    }
};
} // namespace

class StudioExport2Test : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
        qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                     "context");
    }
    void codecsAndRouteOptionsMatchProductionBytes();
    void compensatedDstMatchesSolverAndDocumentIsUnchanged();
    void incompatibleExportPreservesExistingFile();
    void generationOptionsUndoAndPersist();
    void appliqueContoursMatchProduction();
    void realQmlOpensAndChangesExportOptions();
};

void StudioExport2Test::codecsAndRouteOptionsMatchProductionBytes() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig;
    rig.session.replaceProject(fixture(true), true); // vrais arrêts d'appliqué, fils distincts
    const auto revision = rig.session.revision();
    for (const auto* format : {"dst", "pes", "jef", "exp", "vp3"}) {
        for (int mode = 0; mode < 3; ++mode) {
            auto values = rig.exports.options();
            values["reduceTrims"] = mode == 1;
            values["betweenObjects"] = mode == 1;
            values["withinObject"] = mode == 1;
            values["reorderUnderlay"] = mode == 1;
            values["reorderRegions"] = mode == 1;
            values["sameColorSurface"] = mode == 1;
            values["colorCompanion"] = true;
            values["trims"] = mode != 2;
            values["colorChanges"] = mode != 2;
            values["stops"] = mode;
            const auto file = dir.filePath(QStringLiteral("SAMPLE.%1").arg(format));
            rig.files.exportMachine(QUrl::fromLocalFile(file), values); // vraie délégation
            QVERIFY(rig.idle());
            QVERIFY2(rig.exports.lastOk(), qPrintable(rig.exports.message()));
            AdvancedExportOptions request;
            request.machine.design_name = "SAMPLE";
            request.machine.trims = mode == 2 ? formats::TrimMode::Drop : formats::TrimMode::Native;
            request.machine.color_changes =
                mode == 2 ? formats::ColorChangeMode::Drop : formats::ColorChangeMode::Emit;
            request.machine.stops = static_cast<formats::StopMode>(mode);
            request.routes.reduce_trims = mode == 1;
            request.routes.hidden_travel.enabled = mode == 1;
            request.routes.hidden_travel.between_objects = mode == 1;
            request.routes.hidden_travel.within_object = mode == 1;
            request.routes.hidden_travel.reorder_underlay = mode == 1;
            request.routes.hidden_travel.reorder_regions = mode == 1;
            request.routes.hidden_travel.allow_same_color_surface = mode == 1;
            request.routes.hidden_travel.max_bridge_length = Micrometers{20'000};
            const auto reference = dir.filePath(QStringLiteral("REFERENCE.%1").arg(format));
            project_io::ColorCompanionExport companion;
            companion.write = true;
            const auto wrote = project_io::export_machine_file(rig.session.project(), format,
                                                               pathOf(reference), request.machine,
                                                               request.routes, nullptr, &companion);
            QVERIFY2(wrote.has_value(), wrote ? "" : wrote.error().message.c_str());
            QCOMPARE(read(file), read(reference));
            QVERIFY(!read(file).isEmpty());
            if (QString::fromLatin1(format) == "dst" || QString::fromLatin1(format) == "exp") {
                QCOMPARE(read(dir.filePath("SAMPLE.csv")), read(dir.filePath("REFERENCE.csv")));
                QVERIFY(!read(dir.filePath("SAMPLE.csv")).isEmpty());
            }
        }
    }
    QCOMPARE(rig.session.revision(), revision);
    QVERIFY(!rig.session.canUndo());
    rig.files.exportAgain();
    QVERIFY(rig.idle());
    QVERIFY(rig.exports.lastOk());
}

void StudioExport2Test::compensatedDstMatchesSolverAndDocumentIsUnchanged() {
    QTemporaryDir dir;
    Rig rig;
    rig.session.replaceProject(fixture(), true);
    const auto before = stitch_generation::effective_sequence(rig.session.project());
    QVERIFY(before.has_value());
    auto values = rig.exports.options();
    values["compensate"] = true;
    values["strength"] = 0.75;
    const auto file = dir.filePath("compensated.dst");
    rig.exports.exportMachine(QUrl::fromLocalFile(file), values);
    QVERIFY(rig.idle());
    QVERIFY2(rig.exports.lastOk(), qPrintable(rig.exports.message()));
    project_ops::ColorRegistrationOptions options;
    options.compensate = true;
    options.strength = 0.75;
    const auto expected = project_ops::predict_color_registration(rig.session.project(), options);
    QVERIFY(expected.has_value());
    const auto& bytes = expected->compensated_dst;
    QCOMPARE(read(file), QByteArray(reinterpret_cast<const char*>(bytes.data()),
                                    static_cast<qsizetype>(bytes.size())));
    const auto after = stitch_generation::effective_sequence(rig.session.project());
    QVERIFY(after.has_value());
    QCOMPARE(formats::encode_dst(*before).value(), formats::encode_dst(*after).value());
    QVERIFY(!rig.session.canUndo());
}

void StudioExport2Test::incompatibleExportPreservesExistingFile() {
    QTemporaryDir dir;
    const auto file = dir.filePath("protected.dst");
    QFile original(file);
    QVERIFY(original.open(QIODevice::WriteOnly));
    original.write("existing output");
    original.close();
    Rig rig;
    auto p = fixture();
    p.canvas.width = Micrometers{2'000};
    p.canvas.height = Micrometers{2'000};
    rig.session.replaceProject(std::move(p), true);
    rig.exports.exportMachine(QUrl::fromLocalFile(file));
    QVERIFY(rig.idle());
    QVERIFY(!rig.exports.lastOk());
    QVERIFY(rig.exports.message().contains("cadre"));
    QCOMPARE(read(file), QByteArray("existing output"));
    auto machine = fixture();
    machine.machine_id = "brother-pe800";
    // Choix d'un profil réel et d'un format refusé d'après son registre.
    const auto choices = machineChoices();
    QVERIFY(!choices.empty());
    bool exercised = false;
    for (const auto& choice : choices) {
        machine.machine_id = choice.id;
        if (!checkExport(machine, "vp3").formatAccepted) {
            rig.session.replaceProject(machine, true);
            rig.exports.exportMachine(QUrl::fromLocalFile(dir.filePath("refused.vp3")));
            QVERIFY(rig.idle());
            QVERIFY(!rig.exports.lastOk());
            QVERIFY(!QFile::exists(dir.filePath("refused.vp3")));
            exercised = true;
            break;
        }
    }
    QVERIFY(exercised);
    rig.exports.exportMachine(QUrl(QStringLiteral("https://example.test/design.dst")));
    QVERIFY(!rig.exports.lastOk());
}

void StudioExport2Test::generationOptionsUndoAndPersist() {
    QTemporaryDir dir;
    Rig rig;
    rig.session.replaceProject(fixture(), true);
    const auto before = rig.session.project().finishing;
    const auto sequence = stitch_generation::effective_sequence(rig.session.project());
    QVERIFY(sequence.has_value());
    auto values = rig.exports.options();
    values["finishingEnabled"] = true;
    values["splitLongStitches"] = true;
    values["filterShortStitches"] = true;
    values["maxStitchMm"] = 5.0;
    rig.exports.exportMachine(QUrl::fromLocalFile(dir.filePath("finished.dst")), values);
    QVERIFY(rig.idle());
    QVERIFY2(rig.exports.lastOk(), qPrintable(rig.exports.message()));
    QCOMPARE(rig.session.undoNames().size(), std::size_t{1});
    QVERIFY(rig.session.project().finishing.enabled);
    const auto saved =
        project_io::save_project(pathOf(dir.filePath("settings.osp")), rig.session.project());
    QVERIFY(saved.has_value());
    const auto loaded = project_io::load_project(pathOf(dir.filePath("settings.osp")));
    QVERIFY(loaded.has_value());
    QVERIFY(loaded->finishing == rig.session.project().finishing);
    QVERIFY(rig.session.undo());
    QVERIFY(rig.session.project().finishing == before);
    const auto restored = stitch_generation::effective_sequence(rig.session.project());
    QVERIFY(restored.has_value());
    QCOMPARE(formats::encode_dst(*sequence).value(), formats::encode_dst(*restored).value());
    QVERIFY(!rig.session.canUndo());
}

void StudioExport2Test::appliqueContoursMatchProduction() {
    QTemporaryDir dir;
    Rig rig;
    rig.session.replaceProject(fixture(true), true);
    for (const auto* format : {"svg", "dxf"}) {
        const auto file = dir.filePath(QStringLiteral("cut.%1").arg(format));
        rig.exports.exportAppliqueCut(QUrl::fromLocalFile(file));
        QVERIFY(rig.idle());
        QVERIFY2(rig.exports.lastOk(), qPrintable(rig.exports.message()));
        const auto expected = dir.filePath(QStringLiteral("expected.%1").arg(format));
        QVERIFY(
            project_ops::export_applique_cut(rig.session.project(), pathOf(expected)).has_value());
        QCOMPARE(read(file), read(expected));
        QVERIFY(!read(file).isEmpty());
    }
    QVERIFY(!rig.session.canUndo());
}

void StudioExport2Test::realQmlOpensAndChangesExportOptions() {
    Rig rig;
    rig.session.replaceProject(fixture(), true);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("studio", &rig.studio);
    engine.rootContext()->setContextProperty("actions", &rig.actions);
    engine.rootContext()->setContextProperty("editor", &rig.editor);
    engine.rootContext()->setContextProperty("files", &rig.files);
    engine.rootContext()->setContextProperty("machineExport", &rig.exports);
    engine.load(QUrl("qrc:/studio/qml/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    rig.exports.openDialog();
    auto* dialog = window->findChild<QObject*>("advancedExportDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    auto* check = window->findChild<QQuickItem*>("exportReduceTrims");
    QVERIFY(check);
    const auto center = check->mapToScene(QPointF(check->width() / 2, check->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    QTRY_VERIFY(dialog->property("values").toMap().value("reduceTrims").toBool());
    QTemporaryDir dir;
    rig.exports.exportMachine(QUrl::fromLocalFile(dir.filePath("qml.dst")),
                              dialog->property("values").toMap());
    QVERIFY(rig.idle());
    QVERIFY2(rig.exports.lastOk(), qPrintable(rig.exports.message()));
    QVERIFY(!read(dir.filePath("qml.dst")).isEmpty());
}

QTEST_MAIN(StudioExport2Test)
#include "test_studio_export2.moc"
