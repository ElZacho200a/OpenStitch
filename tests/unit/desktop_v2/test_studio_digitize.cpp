// SPDX-License-Identifier: Apache-2.0
// Famille G de Studio : retouche des points, fondus, colonnes satin, guides, texte. Vrai moteur,
// vraies broderies (rectangle tatami du moteur), QTest headless. Chaque parcours vérifie : une
// étape d'annulation par action, refus lisibles sans mutation, retour octet pour octet à la
// séquence d'origine après annulation, persistance OSP.
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

#include <filesystem>

#include "action_controller.hpp"
#include "analysis_controller.hpp"
#include "digitize_gestures.hpp"
#include "edit_controller.hpp"
#include "fade_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "inspector_controller.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "point_edit_controller.hpp"
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
    PointEditController points{studio};
    FadeController fade{studio};
    SatinController satin{studio};
    ActionController actions{studio};
    std::uint64_t id{0};

    Rig() {
        auto created = project_ops::create_empty_project({});
        if (created) {
            session.replaceProject(std::move(*created), true);
        }
    }
    void addRect() {
        const auto r = session.applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle");
        QVERIFY(r.committed);
        id = session.project().embroidery_objects.front().id.value;
    }
    void select(std::uint64_t value) { studio.selectObject(QString::number(value)); }
    [[nodiscard]] std::size_t steps() const { return session.undoNames().size(); }
};

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
QQuickItem* named(QQuickWindow* window, const char* name) {
    return findVisual(window->contentItem(), QString::fromLatin1(name));
}
bool press(QQuickWindow* window, const char* name) {
    auto* item = named(window, name);
    return item && item->isEnabled() && QMetaObject::invokeMethod(item, "click");
}

QString fontFile() {
    return QString::fromStdString(
        (std::filesystem::path(OPENSTITCH_FONTS_DIR) / "VeraBd.ttf").string());
}
} // namespace

class StudioDigitizeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void statusSignalsDoNotRecalculateDigitizeGeometry();
    void pointEditJourney();
    void pointEditPersistsInOsp();
    void fadeJourney();
    void fadeMatchesTheService();
    void satinColumnJourney();
    void directionGuidesJourney();
    void autoSatinThroughTheActionPalette();
    void textLetteringJourney();
    void mainQmlHostsDigitizePanels();
};

void StudioDigitizeTest::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                 QStringLiteral("Provided by the application"));
}

void StudioDigitizeTest::statusSignalsDoNotRecalculateDigitizeGeometry() {
    Rig rig;
    rig.addRect();
    rig.select(rig.id);
    rig.points.setActive(true);
    DigitizeGestures gestures(rig.studio, rig.points, rig.fade, rig.satin);
    gestures.setMode(QStringLiteral("points"));

    QSignalSpy pointsChanged(&rig.points, &PointEditController::changed);
    QSignalSpy fadeChanged(&rig.fade, &FadeController::changed);
    QSignalSpy satinChanged(&rig.satin, &SatinController::changed);
    QSignalSpy overlaysChanged(&gestures, &DigitizeGestures::overlayChanged);

    // Chargement, progression et état métier sans mutation : strictement zéro recalcul.
    QVERIFY(QMetaObject::invokeMethod(&rig.studio, "changed", Qt::DirectConnection));
    QCOMPARE(pointsChanged.count(), 0);
    QCOMPARE(fadeChanged.count(), 0);
    QCOMPARE(satinChanged.count(), 0);
    QCOMPARE(overlaysChanged.count(), 0);

    // Changer réellement la sélection doit toujours actualiser chaque adaptateur,
    // mais uniquement la surcouche du mode de numérisation actif.
    rig.studio.clearSelection();
    QCOMPARE(pointsChanged.count(), 1);
    QCOMPARE(fadeChanged.count(), 1);
    QCOMPARE(satinChanged.count(), 1);
    QCOMPARE(overlaysChanged.count(), 1);
}

void StudioDigitizeTest::pointEditJourney() {
    Rig rig;
    rig.addRect();
    const auto base = sequenceOf(rig.session);
    QVERIFY(!base.commands.empty());
    QCOMPARE(rig.points.state(), QStringLiteral("none")); // not active, nothing selected
    rig.select(rig.id);
    rig.points.setActive(true);
    QTRY_COMPARE(rig.points.objectId(), QString::number(rig.id));
    QCOMPARE(rig.points.state(), QStringLiteral("clean"));
    QVERIFY(rig.points.pointCount() > 8);

    // Refusals leave the document untouched.
    const auto depth = rig.steps();
    QVERIFY(!rig.points.deleteSelected());
    QVERIFY(!rig.points.lastOk());
    QVERIFY(!rig.points.message().isEmpty());
    QVERIFY(!rig.points.movePoint(99'999'999, 1, 1));
    QCOMPARE(rig.steps(), depth);

    const auto all = rig.points.pointsIn(-1000, -1000, 1000, 1000, 0);
    QVERIFY(all.size() > 8);
    int first = -1;
    for (const auto& v : all) {
        if (v.toMap().value(QStringLiteral("movable")).toBool()) {
            first = v.toMap().value(QStringLiteral("index")).toInt();
            break;
        }
    }
    QVERIFY(first >= 0);
    const auto info = rig.points.pointInfo(first);
    QCOMPARE(rig.points.pointAt(info.value("x").toDouble(), info.value("y").toDouble(), 0.01),
             first);

    // Simple selection, move, one undo step, exact return to the generated sequence.
    QVERIFY(rig.points.selectPoint(first, false));
    QCOMPARE(rig.points.selectedCount(), 1);
    QVERIFY(rig.points.moveSelected(0.3, 0.2));
    QCOMPARE(rig.steps(), depth + 1);
    QTRY_COMPARE(rig.points.state(), QStringLiteral("edited"));
    QCOMPARE(rig.points.editedCount(), 1);
    QVERIFY(sequenceOf(rig.session).commands != base.commands);
    QVERIFY(rig.session.undo());
    QTRY_COMPARE(rig.points.state(), QStringLiteral("clean"));
    QVERIFY(sequenceOf(rig.session).commands == base.commands);

    // Range selection: delete, restore, jump <-> stitch, trim; each one undo step.
    QVERIFY(rig.points.selectRange(first, first + 3));
    QVERIFY(rig.points.selectedCount() >= 2);
    QVERIFY(rig.points.deleteSelected());
    QCOMPARE(rig.steps(), depth + 1);
    QVERIFY(sequenceOf(rig.session).commands != base.commands);
    QVERIFY(rig.points.restoreSelected());
    QCOMPARE(rig.steps(), depth + 2);
    QVERIFY(rig.points.setJump(true));
    QCOMPARE(rig.steps(), depth + 3);
    QVERIFY(rig.points.setJump(false));
    QVERIFY(rig.points.setTrim(true));
    QVERIFY(rig.points.setTrim(false));
    QVERIFY(rig.points.insertAfter(first, info.value("x").toDouble() + 0.2,
                                   info.value("y").toDouble()));
    QVERIFY(rig.points.selectAllPoints() > 8);

    // Abandon: back to the generation, byte for byte.
    QVERIFY(rig.points.discardEdits());
    QTRY_COMPARE(rig.points.state(), QStringLiteral("clean"));
    QVERIFY(sequenceOf(rig.session).commands == base.commands);
    rig.points.setActive(false);
    QCOMPARE(rig.points.pointCount(), 0);
}

void StudioDigitizeTest::pointEditPersistsInOsp() {
    QTemporaryDir dir;
    const auto path = QUrl::fromLocalFile(dir.filePath(QStringLiteral("points.osp")));
    stitch::StitchSequence edited;
    {
        Rig rig;
        rig.addRect();
        rig.select(rig.id);
        rig.points.setActive(true);
        QTRY_COMPARE(rig.points.objectId(), QString::number(rig.id));
        const auto all = rig.points.pointsIn(-1000, -1000, 1000, 1000, 0);
        const int index = all.at(2).toMap().value(QStringLiteral("index")).toInt();
        QVERIFY(rig.points.selectPoint(index, false));
        QVERIFY(rig.points.moveSelected(0.4, 0.0));
        edited = sequenceOf(rig.session);
        QVERIFY(rig.studio.saveProjectAs(path));
    }
    Rig reopened;
    QSignalSpy spy(&reopened.studio, &StudioController::loadFinished);
    reopened.studio.openProject(path);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() == 1, 15'000);
    QVERIFY(spy.takeFirst().at(0).toBool());
    QVERIFY(sequenceOf(reopened.session).commands == edited.commands);
}

void StudioDigitizeTest::fadeJourney() {
    Rig rig;
    rig.addRect();
    rig.select(rig.id);
    QTRY_COMPARE(rig.fade.objectId(), QString::number(rig.id));
    QVERIFY(rig.fade.canCreate());
    QVERIFY(!rig.fade.hasFade());
    const auto base = sequenceOf(rig.session);
    const auto depth = rig.steps();

    QVERIFY(rig.fade.create(QColor(200, 30, 30), true));
    QCOMPARE(rig.steps(), depth + 1);
    rig.select(rig.session.project().embroidery_objects.front().id.value);
    QTRY_VERIFY(rig.fade.hasFade());
    QVERIFY(!rig.fade.axis().isEmpty());
    QVERIFY(!rig.fade.summary().isEmpty());
    QCOMPARE(rig.fade.curves().size(), 5);
    QCOMPARE(rig.fade.curveProfile().size(), 32);
    const auto faded = sequenceOf(rig.session);

    // Handle drag: the preview never touches the document, the commit is ONE command.
    const auto revision = rig.session.revision();
    const auto axis = rig.fade.axis();
    const double endX = axis.value("end").toMap().value("x").toDouble();
    const double endY = axis.value("end").toMap().value("y").toDouble();
    const auto preview = rig.fade.previewHandle(1, endX - 2.0, endY, false);
    QVERIFY(!preview.isEmpty());
    QCOMPARE(rig.session.revision(), revision);
    Rig again;
    again.addRect();
    again.select(again.id);
    QVERIFY(again.fade.create(QColor(200, 30, 30), true));
    again.select(again.session.project().embroidery_objects.front().id.value);
    QTRY_VERIFY(again.fade.hasFade());
    const auto d2 = again.steps();
    QVERIFY(again.fade.commitHandle(1, endX - 2.0, endY, false));
    QCOMPARE(again.steps(), d2 + 1);
    QVERIFY(sequenceOf(again.session).commands != faded.commands);
    QVERIFY(again.fade.setCurve(1));
    QCOMPARE(again.steps(), d2 + 2);
    QCOMPARE(again.fade.curve(), 1);
    QVERIFY(!again.fade.setCurve(42)); // unknown curve: readable refusal
    QVERIFY(!again.fade.lastOk());
    QVERIFY(again.session.undo());
    QVERIFY(again.session.undo());
    QVERIFY(sequenceOf(again.session).commands == faded.commands);

    // Edit then remove, all undoable.
    QVERIFY(again.fade.edit(QColor(10, 120, 200), false));
    QVERIFY(!again.fade.complementary());
    QVERIFY(again.fade.remove());
    QTRY_VERIFY(!again.fade.hasFade());

    // Undo of the creation restores the plain fill exactly.
    Rig plain;
    plain.addRect();
    plain.select(plain.id);
    QVERIFY(plain.fade.create(QColor(200, 30, 30), true));
    QVERIFY(plain.session.undo());
    QVERIFY(sequenceOf(plain.session).commands == base.commands);

    // Not a fill / nothing selected: refusal with a reason, no mutation.
    Rig none;
    const auto before = none.steps();
    QVERIFY(!none.fade.create(QColor(1, 2, 3), true));
    QVERIFY(!none.fade.message().isEmpty());
    QCOMPARE(none.steps(), before);
}

void StudioDigitizeTest::fadeMatchesTheService() {
    // Same parameters through the controller and through the core service: identical sequences.
    Rig viaController;
    viaController.addRect();
    viaController.select(viaController.id);
    QVERIFY(viaController.fade.create(QColor(200, 30, 30), true));
    Rig viaService;
    viaService.addRect();
    QVERIFY(createFade(viaService.session, viaService.id, FadeSettings{{200, 30, 30}, true}).ok);
    QVERIFY(sequenceOf(viaController.session).commands == sequenceOf(viaService.session).commands);
}

void StudioDigitizeTest::satinColumnJourney() {
    Rig rig;
    const auto depth = rig.steps();
    QVERIFY(rig.satin.createFromRungs({}).isEmpty()); // refused: no rung
    QVERIFY(!rig.satin.lastOk());
    QCOMPARE(rig.steps(), depth);
    const auto rung = [](double x, double y0, double y1) {
        return QVariantMap{{"ax", x}, {"ay", y0}, {"bx", x}, {"by", y1}};
    };
    const auto id = rig.satin.createFromRungs({rung(-10, 0, 4), rung(0, 1, 5), rung(10, 0, 4)});
    QVERIFY(!id.isEmpty());
    QCOMPARE(rig.steps(), depth + 1);
    QTRY_VERIFY(rig.satin.isSatin());
    QCOMPARE(rig.satin.railNodesA(), 3);
    QCOMPARE(rig.satin.railNodesB(), 3);
    QCOMPARE(rig.satin.rungCount(), 3);
    QVERIFY(rig.satin.clean());
    QVERIFY(!rig.satin.diagnosis().isEmpty());
    QCOMPARE(rig.satin.rungs().size(), 3);
    const auto created = sequenceOf(rig.session);
    QVERIFY(!created.commands.empty());

    QVERIFY(rig.satin.moveRailNode("A", 1, 0.0, 1.5));
    QCOMPARE(rig.steps(), depth + 2);
    QVERIFY(sequenceOf(rig.session).commands != created.commands);
    QVERIFY(rig.session.undo());
    QVERIFY(sequenceOf(rig.session).commands == created.commands);

    QVERIFY(rig.satin.insertRailNode("B", 0, 0.5));
    QCOMPARE(rig.satin.railNodesB(), 4);
    QVERIFY(rig.satin.addRung(1, -5, 0.5, -5, 4.5));
    QCOMPARE(rig.satin.rungCount(), 4);
    QVERIFY(rig.satin.moveRung(1, -5, 0.5, -5, 4.0));
    QVERIFY(rig.satin.insertRungBetween(0));
    QVERIFY(rig.satin.removeRung(1));
    QVERIFY(rig.satin.widestGap() >= -1);
    QVERIFY(rig.satin.redundantRungIndex() >= -1);

    // Refusals: bad rail / index leave the document untouched.
    const auto before = rig.steps();
    QVERIFY(!rig.satin.moveRailNode("Z", 0, 0, 0));
    QVERIFY(!rig.satin.removeRung(999));
    QVERIFY(!rig.satin.lastOk());
    QVERIFY(!rig.satin.message().isEmpty());
    QCOMPARE(rig.steps(), before);

    // Not a satin: no rails reported.
    Rig fill;
    fill.addRect();
    fill.select(fill.id);
    QTRY_VERIFY(!fill.satin.isSatin());
}

void StudioDigitizeTest::directionGuidesJourney() {
    Rig rig;
    rig.addRect();
    rig.select(rig.id);
    QTRY_COMPARE(rig.satin.objectId(), QString::number(rig.id));
    const auto base = sequenceOf(rig.session);
    const auto depth = rig.steps();
    const auto point = [](double x, double y) { return QVariantMap{{"x", x}, {"y", y}}; };
    QVERIFY(!rig.satin.addGuide({point(0, 0)}, false)); // one point: refused
    QCOMPARE(rig.steps(), depth);
    QVERIFY(rig.satin.addGuide({point(-10, -5), point(0, 0), point(10, 5)}, false));
    QCOMPARE(rig.steps(), depth + 1); // tatami -> directional + guide in ONE step
    QTRY_VERIFY(rig.satin.guideCount() >= 1);
    QVERIFY(rig.satin.addGuide({point(-8, 8), point(8, -8)}, true));
    QTRY_COMPARE(rig.satin.breakLineCount(), 1);
    QVERIFY(rig.satin.clearGuides(true));
    QVERIFY(rig.satin.clearGuides(false));
    QTRY_COMPARE(rig.satin.guideCount(), 0);
    QVERIFY(rig.session.undo());
    QVERIFY(rig.session.undo());
    QVERIFY(rig.session.undo());
    QVERIFY(rig.session.undo());
    QVERIFY(sequenceOf(rig.session).commands == base.commands);
}

void StudioDigitizeTest::autoSatinThroughTheActionPalette() {
    Rig rig;
    const auto r = rig.session.applyBatch(batchAddRect({0, 0}, 40, 6), "Bande");
    QVERIFY(r.committed);
    rig.id = *createdObjectId(r.report);
    const auto base = sequenceOf(rig.session);
    rig.select(rig.id);
    const auto depth = rig.steps();
    QVERIFY(!rig.actions.run(QStringLiteral("satin.auto"), {{"spacing_mm", "9"}}).isEmpty());
    QCOMPARE(rig.steps(), depth); // out of range: refused, nothing changed
    for (const char* strategy : {"skeleton", "parallel_sweep", "medial_axis"}) {
        QCOMPARE(rig.actions.run(QStringLiteral("satin.auto"),
                                 {{"spacing_mm", "0.35"}, {"strategy", strategy}}),
                 QString());
        QCOMPARE(rig.steps(), depth + 1);
        QVERIFY(sequenceOf(rig.session).commands != base.commands);
        QVERIFY(rig.session.undo());
        QVERIFY(sequenceOf(rig.session).commands == base.commands);
    }
}

void StudioDigitizeTest::textLetteringJourney() {
    Rig rig;
    const auto depth = rig.steps();
    QVERIFY(!rig.actions.run(QStringLiteral("text.add"), {{"text", ""}, {"font_file", fontFile()}})
                 .isEmpty()); // empty text: refused
    QCOMPARE(rig.steps(), depth);
    QCOMPARE(rig.actions.run(QStringLiteral("text.add"), {{"text", "Hello"},
                                                          {"cap_height_mm", "15"},
                                                          {"base", "arc"},
                                                          {"base_radius_mm", "40"},
                                                          {"font_file", fontFile()}}),
             QString());
    QCOMPARE(rig.steps(), depth + 1);
    QCOMPARE(rig.session.project().text_objects.size(), std::size_t{1});
    const auto lettered = sequenceOf(rig.session);
    QVERIFY(!lettered.commands.empty());

    // Select the letters, edit the text in one step, remove it in one step.
    rig.studio.clearSelection();
    for (const auto& e : rig.session.project().embroidery_objects) {
        rig.studio.selectObject(QString::number(e.id.value), true);
    }
    QCOMPARE(rig.actions.run(QStringLiteral("text.edit"), {{"text", "Salut"}, {"base", "line"}}),
             QString());
    QCOMPARE(rig.steps(), depth + 2);
    QCOMPARE(QString::fromStdString(rig.session.project().text_objects.front().text),
             QStringLiteral("Salut"));
    QVERIFY(rig.session.undo());
    QVERIFY(sequenceOf(rig.session).commands == lettered.commands);
    QVERIFY(rig.session.redo());
    rig.studio.clearSelection();
    for (const auto& e : rig.session.project().embroidery_objects) {
        rig.studio.selectObject(QString::number(e.id.value), true);
    }
    QCOMPARE(rig.actions.run(QStringLiteral("text.remove"), {}), QString());
    QVERIFY(rig.session.project().text_objects.empty());
    QVERIFY(rig.session.undo());
    QCOMPARE(rig.session.project().text_objects.size(), std::size_t{1});
}

void StudioDigitizeTest::mainQmlHostsDigitizePanels() {
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
    SatinController satin(studio);
    FadeController fade(studio);
    PointEditController pointEdit(studio);
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
    ctx->setContextProperty(QStringLiteral("satin"), &satin);
    ctx->setContextProperty(QStringLiteral("fade"), &fade);
    ctx->setContextProperty(QStringLiteral("pointEdit"), &pointEdit);
    engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    auto project = project_ops::create_empty_project({});
    QVERIFY(project.has_value());
    session.replaceProject(std::move(*project), true);
    QVERIFY(session.applyBatch(batchAddRect({0, 0}, 40, 20), "Rectangle").committed);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && studio.scene() != nullptr, 15'000);
    auto* hub = named(window, "digitizeHub");
    QVERIFY(hub);
    QTRY_VERIFY(hub->property("ready").toBool());
    const auto id = session.project().embroidery_objects.front().id.value;
    studio.selectObject(QString::number(id));

    // Point edit panel drives the real controller.
    QVERIFY(press(window, "pointEditPanelButton"));
    QCOMPARE(hub->property("openPanel").toString(), QStringLiteral("points"));
    QVERIFY(press(window, "pointEditToggle"));
    QTRY_VERIFY(pointEdit.active());
    QTRY_VERIFY(pointEdit.pointCount() > 8);
    QVERIFY(press(window, "pointSelectAll"));
    QVERIFY(pointEdit.selectedCount() > 8);
    const auto depth = session.undoNames().size();
    QVERIFY(press(window, "pointToJump"));
    QCOMPARE(session.undoNames().size(), depth + 1);
    QVERIFY(press(window, "pointDiscard"));
    QTRY_COMPARE(pointEdit.state(), QStringLiteral("clean"));

    // F26-02: the real QML form must reject malformed distances, accept French
    // decimal commas as 0.5 mm (not parseFloat's erroneous 0 mm), and preserve undo.
    auto* dxField = named(window, "pointMoveDx");
    auto* dyField = named(window, "pointMoveDy");
    auto* moveButton = named(window, "pointMove");
    QVERIFY(dxField && dyField && moveButton);
    const auto pointList = pointEdit.pointsIn(-1000, -1000, 1000, 1000, 0);
    int firstMovable = -1;
    for (const auto& item : pointList) {
        const auto point = item.toMap();
        if (point.value(QStringLiteral("movable")).toBool()) {
            firstMovable = point.value(QStringLiteral("index")).toInt();
            break;
        }
    }
    QVERIFY(firstMovable >= 0);
    QVERIFY(pointEdit.selectPoint(firstMovable, false));
    const auto beforeMove = sequenceOf(session);
    const auto beforeMoveDepth = session.undoNames().size();

    QVERIFY(dxField->setProperty("text", QStringLiteral("0,5mm")));
    QTRY_VERIFY(!moveButton->isEnabled());
    QVERIFY(dxField->property("invalid").toBool());
    QCOMPARE(session.undoNames().size(), beforeMoveDepth);
    QVERIFY(dxField->setProperty("text", QStringLiteral("")));
    QTRY_VERIFY(!moveButton->isEnabled());
    QVERIFY(dxField->setProperty("text", QStringLiteral("0,5")));
    QVERIFY(dyField->setProperty("text", QStringLiteral("0")));
    QTRY_VERIFY(moveButton->isEnabled());
    QVERIFY(press(window, "pointMove"));
    QCOMPARE(session.undoNames().size(), beforeMoveDepth + 1);
    QVERIFY(sequenceOf(session).commands != beforeMove.commands);
    QVERIFY(session.undo());
    QVERIFY(sequenceOf(session).commands == beforeMove.commands);

    // Fade panel.
    QVERIFY(press(window, "fadePanelButton"));
    QTRY_VERIFY(fade.canCreate());
    QVERIFY(press(window, "fadeCreate"));
    studio.selectObject(QString::number(session.project().embroidery_objects.front().id.value));
    QTRY_VERIFY(fade.hasFade());
    QVERIFY(press(window, "fadeCurve2"));
    QCOMPARE(fade.curve(), 2);
    QVERIFY(press(window, "fadeRemove"));
    QTRY_VERIFY(!fade.hasFade());

    // Satin panel: F19-02, numeric rail moves use full validated values
    // and bound node indices, not partial parseFloat("0,5") == 0.
    QVERIFY(press(window, "satinPanelButton"));
    QVERIFY(named(window, "satinPanel")->isVisible());
    // F23-01/F23-02: actual QML controls create undoable directional guides
    // and break lines using millimetre coordinates (without any stub action).
    auto* startX = named(window, "guideStartX");
    auto* startY = named(window, "guideStartY");
    auto* endX = named(window, "guideEndX");
    auto* endY = named(window, "guideEndY");
    auto* addGuideButton = named(window, "addDirectionGuide");
    auto* addBreakButton = named(window, "addBreakLine");
    QVERIFY(startX && startY && endX && endY && addGuideButton && addBreakButton);
    QVERIFY(startX->setProperty("text", QStringLiteral("-10mm")));
    QVERIFY(startY->setProperty("text", QStringLiteral("-5")));
    QVERIFY(endX->setProperty("text", QStringLiteral("10")));
    QVERIFY(endY->setProperty("text", QStringLiteral("5")));
    QTRY_VERIFY(!addGuideButton->isEnabled());
    QVERIFY(startX->setProperty("text", QStringLiteral("-10,0")));
    QTRY_VERIFY(addGuideButton->isEnabled());
    const auto guideDepth = session.undoNames().size();
    QVERIFY(press(window, "addDirectionGuide"));
    QCOMPARE(session.undoNames().size(), guideDepth + 1);
    QTRY_VERIFY(satin.guideCount() >= 1);
    QVERIFY(press(window, "addBreakLine"));
    QCOMPARE(session.undoNames().size(), guideDepth + 2);
    QTRY_VERIFY(satin.breakLineCount() >= 1);
    QVERIFY(session.undo());
    QVERIFY(session.undo());
    const auto rung = [](double x, double y0, double y1) {
        return QVariantMap{{"ax", x}, {"ay", y0}, {"bx", x}, {"by", y1}};
    };
    const auto satinId = satin.createFromRungs(
        {rung(-10, 0, 4), rung(0, 1, 5), rung(10, 0, 4)});
    QVERIFY(!satinId.isEmpty());
    studio.selectObject(satinId);
    QTRY_VERIFY(satin.isSatin());
    auto* xField = named(window, "satinNodeX");
    auto* yField = named(window, "satinNodeY");
    auto* railNode = named(window, "satinNode");
    auto* moveRail = named(window, "satinMoveNode");
    QVERIFY(xField && yField && railNode && moveRail);
    QTRY_COMPARE(railNode->property("to").toInt(), 2);
    QVERIFY(xField->setProperty("text", QStringLiteral("0,5mm")));
    QVERIFY(yField->setProperty("text", QStringLiteral("1,5")));
    QTRY_VERIFY(!moveRail->isEnabled());
    QVERIFY(xField->property("invalid").toBool());
    QVERIFY(xField->setProperty("text", QStringLiteral("0,5")));
    QTRY_VERIFY(moveRail->isEnabled());
    const auto railDepth = session.undoNames().size();
    QVERIFY(press(window, "satinMoveNode"));
    QCOMPARE(session.undoNames().size(), railDepth + 1);
    QVERIFY(session.undo());
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

QTEST_MAIN(StudioDigitizeTest)
#include "test_studio_digitize.moc"
