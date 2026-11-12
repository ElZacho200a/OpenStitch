// SPDX-License-Identifier: Apache-2.0
// Studio -- famille « sélection, transformations, calques, édition d'objets » : parcours réels
// (QML, canevas, souris QTest, moteur) sur un vrai projet, annulation en une étape, enregistrement
// puis réouverture. Aucun mock du moteur.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <filesystem>

#include "action_controller.hpp"
#include "edit_controller.hpp"
#include "gpu_canvas.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "studio_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

// Identifiants objet de broderie 101..105 (forme source = id - 90), calques 201 (fond) et 202.
constexpr std::uint64_t kRed1 = 101, kGreen = 102, kRed2 = 103, kBlue = 104, kLone = 105;

struct Line {
    std::uint64_t id;
    double x0, x1, y; // mm modèle, Y vers le haut
    std::array<std::uint8_t, 3> rgb;
    std::uint64_t calque;
};

document::Project editFixture() {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    project.calques = {{ObjectId{201}, "Fond", true, false},
                       {ObjectId{202}, "Dessus", true, false}};
    const Line lines[] = {{kRed1, -15, 15, 10, {200, 20, 30}, 201},
                          {kGreen, -15, 15, -10, {20, 180, 60}, 201},
                          {kRed2, 20, 30, 0, {200, 20, 30}, 202},
                          {kBlue, -5, 5, 10.3, {20, 40, 200}, 202},
                          {kLone, 40, 50, 25, {20, 180, 60}, 201}};
    for (const auto& line : lines) {
        document::VectorObject vector;
        vector.id = ObjectId{line.id - 90};
        vector.calque = ObjectId{line.calque};
        geometry::Path path;
        path.closed = false;
        const auto y = Micrometers{static_cast<std::int32_t>(line.y * 1000)};
        path.nodes = {{{Micrometers{static_cast<std::int32_t>(line.x0 * 1000)}, y}},
                      {{Micrometers{static_cast<std::int32_t>(line.x1 * 1000)}, y}}};
        vector.paths.push_back({path, {}});
        project.vector_objects.push_back(vector);
        document::EmbroideryObject object;
        object.id = ObjectId{line.id};
        object.source_vector = vector.id;
        object.name = "Ligne " + std::to_string(line.id);
        object.rgb = line.rgb;
        object.params = document::RunningStitchParams{Micrometers{2'000}};
        project.embroidery_objects.push_back(object);
    }
    project.object_ids.reset(300);
    return project;
}

const document::VectorObject& shapeOf(const document::Project& p, std::uint64_t objectId) {
    return *p.findObject(ObjectId{objectId - 90});
}

double nodeX(const document::Project& p, std::uint64_t objectId, std::size_t node) {
    return shapeOf(p, objectId).paths.front().outer.nodes[node].pos.x.value / 1000.0;
}
double nodeY(const document::Project& p, std::uint64_t objectId, std::size_t node) {
    return shapeOf(p, objectId).paths.front().outer.nodes[node].pos.y.value / 1000.0;
}

QPoint windowPoint(QQuickItem& item, ScenePoint point) {
    return item.mapToScene(QPointF(point.x, point.y)).toPoint();
}

// Les délégués d'un Repeater sont des enfants VISUELS : `findChild` (arbre QObject) ne les voit
// pas.
QQuickItem* deepItem(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) {
        return root;
    }
    for (auto* child : root->childItems()) {
        if (auto* found = deepItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

void dragMouse(QQuickWindow* window, QPoint from, QPoint to, Qt::KeyboardModifiers mods = {}) {
    QTest::mousePress(window, Qt::LeftButton, mods, from);
    for (int step = 1; step <= 6; ++step) {
        // Les modificateurs tenus pendant le geste accompagnent aussi les déplacements (Maj =
        // rotation par pas de 15 degres, jugee a chaque deplacement).
        QTest::mouseEvent(QTest::MouseMove, window, Qt::NoButton, mods,
                          from + (to - from) * step / 6);
    }
    QTest::mouseRelease(window, Qt::LeftButton, mods, to);
}

struct Rig {
    DocumentSession session;
    StudioController controller{session};
    ActionController actions{controller};
    EditController editor{controller};
    QQmlApplicationEngine engine;
    QStringList warnings;
    QQuickWindow* window{nullptr};
    GpuCanvas* canvas{nullptr};
};

} // namespace

class StudioEditTest : public QObject {
    Q_OBJECT
private:
    void open(Rig& rig, document::Project project = editFixture());
    void settle(Rig& rig);
    [[nodiscard]] std::size_t steps(Rig& rig) { return rig.session.undoNames().size(); }
    [[nodiscard]] QPoint at(Rig& rig, double xMm, double yModelMm) {
        // Le canevas affiche le repère scène : Y inversé par rapport au modèle.
        return windowPoint(*rig.canvas, rig.canvas->camera().worldToScreen({xMm, -yModelMm}));
    }
    void select(Rig& rig, std::initializer_list<std::uint64_t> ids) {
        std::vector<Target> targets;
        for (const auto id : ids) {
            targets.push_back({TargetKind::EmbroideryObject, id});
        }
        rig.session.selection().replace(targets);
    }
    [[nodiscard]] std::vector<std::uint64_t> selected(Rig& rig) {
        std::vector<std::uint64_t> ids;
        for (const auto& t : rig.session.selection().targets()) {
            ids.push_back(t.id);
        }
        std::ranges::sort(ids);
        return ids;
    }

private slots:
    void initTestCase();
    void selectionCommandsOnRealObjects();
    void objectLockAndMoveToLayerAreUndoable();
    void rotationAroundAChosenCentreIsOneUndoStep();
    void duplicatingALayerCopiesItsObjectsInOneStep();
    void rectangleSelectionByMouse();
    void savedSelectionsAreRecalled();
    void transformsAreOneUndoStepAndPersist();
    void invalidTransformsAreRefused();
    void placementInMillimetres();
    void alignDistributeStackGroup();
    void mouseMoveWithSnapGuidesAndUndo();
    void handlesResizeAndRotate();
    void layersTreeAndObjectEditing();
    void contextMenuAndEyedropper();
    void shortcutsReachTheEditor();
    void focusedFieldOwnsArrowKeysDeleteAndEscape();
};

void StudioEditTest::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                 QStringLiteral("Provided by the application"));
    qmlRegisterUncreatableType<EditController>("OpenStitch.Studio", 1, 0, "EditController",
                                               QStringLiteral("Provided by the application"));
}

void StudioEditTest::open(Rig& rig, document::Project project) {
    connect(&rig.engine, &QQmlApplicationEngine::warnings, this,
            [&rig](const QList<QQmlError>& errors) {
                for (const auto& error : errors) {
                    rig.warnings.push_back(error.toString());
                }
            });
    rig.engine.rootContext()->setContextProperty(QStringLiteral("studio"), &rig.controller);
    rig.engine.rootContext()->setContextProperty(QStringLiteral("actions"), &rig.actions);
    rig.engine.rootContext()->setContextProperty(QStringLiteral("editor"), &rig.editor);
    rig.engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
    QVERIFY2(!rig.engine.rootObjects().isEmpty(), qPrintable(rig.warnings.join('\n')));
    rig.window = qobject_cast<QQuickWindow*>(rig.engine.rootObjects().front());
    QVERIFY(rig.window);
    rig.canvas = rig.window->findChild<GpuCanvas*>(QStringLiteral("studioCanvas"));
    QVERIFY(rig.canvas);
    rig.session.replaceProject(std::move(project), true);
    settle(rig);
    QTRY_VERIFY_WITH_TIMEOUT(rig.canvas->geometryBuildCount() > 0, 10'000);
}

void StudioEditTest::settle(Rig& rig) {
    QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy() && rig.controller.scene() &&
                                 rig.controller.scene()->revision == rig.session.revision(),
                             15'000);
}

void StudioEditTest::selectionCommandsOnRealObjects() {
    Rig rig;
    open(rig);
    QCOMPARE(rig.editor.selectAll(), 5);
    QCOMPARE(selected(rig).size(), std::size_t{5});
    QCOMPARE(rig.editor.invertSelection(), 0);
    QVERIFY(rig.session.selection().empty());

    select(rig, {kRed1});
    QCOMPARE(rig.editor.selectSameColor(), 2);
    QCOMPARE(selected(rig), (std::vector<std::uint64_t>{kRed1, kRed2}));

    select(rig, {kRed1});
    QCOMPARE(rig.editor.selectNeighbors(), 2); // le bleu, à 0,3 mm, touche la première ligne
    QCOMPARE(selected(rig), (std::vector<std::uint64_t>{kRed1, kBlue}));

    QCOMPARE(rig.editor.selectByType(QStringLiteral("running")), 5);
    QCOMPARE(rig.editor.selectByType(QStringLiteral("inconnu")), 0);
    QCOMPARE(rig.editor.selectByLayer(QStringLiteral("201")), 3);
    QCOMPARE(selected(rig), (std::vector<std::uint64_t>{kRed1, kGreen, kLone}));
    QCOMPARE(rig.editor.selectByLayer(QStringLiteral("202")), 2);

    // Un objet masqué n'est ni sélectionné par « tout » ni par l'inversion.
    QVERIFY(rig.editor.setObjectVisible(QString::number(kLone), false));
    settle(rig);
    QCOMPARE(rig.editor.selectAll(), 4);
    rig.editor.selectNone();
    QVERIFY(rig.session.selection().empty());
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

// Verrouiller un objet et le déplacer vers un autre calque : une étape d'annulation chacun ;
// objet ou calque inconnu refusé sans rien changer.
void StudioEditTest::objectLockAndMoveToLayerAreUndoable() {
    Rig rig;
    open(rig);
    const auto base = steps(rig);
    const QString lone = QString::number(kLone);
    QVERIFY(!rig.session.project().findEmbroidery(ObjectId{kLone})->locked);
    QVERIFY(rig.editor.setObjectLocked(lone, true));
    QVERIFY(rig.session.project().findEmbroidery(ObjectId{kLone})->locked);
    QCOMPARE(steps(rig), base + 1);
    QVERIFY(rig.session.undo());
    QVERIFY(!rig.session.project().findEmbroidery(ObjectId{kLone})->locked);

    QCOMPARE(shapeOf(rig.session.project(), kLone).calque.value, std::uint64_t{201});
    const auto before = steps(rig);
    QVERIFY(rig.editor.moveObjectToLayer(lone, QStringLiteral("202")));
    QCOMPARE(shapeOf(rig.session.project(), kLone).calque.value, std::uint64_t{202});
    QCOMPARE(steps(rig), before + 1);
    QVERIFY(rig.session.undo());
    QCOMPARE(shapeOf(rig.session.project(), kLone).calque.value, std::uint64_t{201});

    const auto stable = steps(rig);
    QVERIFY(!rig.editor.moveObjectToLayer(QStringLiteral("999999"), QStringLiteral("202")));
    QVERIFY(!rig.editor.moveObjectToLayer(lone, QStringLiteral("not-a-layer")));
    QVERIFY(!rig.editor.setObjectLocked(QStringLiteral("not-an-id"), true));
    QCOMPARE(steps(rig), stable);
    QCOMPARE(shapeOf(rig.session.project(), kLone).calque.value, std::uint64_t{201});
}

// Dupliquer un calque : « <nom> (copie) » avec une copie de chaque forme et de chaque objet de
// broderie, en une seule étape d'annulation ; calque inconnu refusé.
void StudioEditTest::duplicatingALayerCopiesItsObjectsInOneStep() {
    Rig rig;
    open(rig);
    const auto base = steps(rig);
    const auto objectsBefore = rig.session.project().embroidery_objects.size();
    const auto shapesBefore = rig.session.project().vector_objects.size();
    const auto layersBefore = rig.editor.layers().size();
    QVERIFY(!rig.editor.duplicateLayer(QStringLiteral("999999")));
    QCOMPARE(steps(rig), base);

    QVERIFY(rig.editor.duplicateLayer(QStringLiteral("201")));
    QCOMPARE(steps(rig), base + 1);
    QCOMPARE(rig.editor.layers().size(), layersBefore + 1);
    QCOMPARE(rig.session.project().embroidery_objects.size(), objectsBefore + 3);
    QCOMPARE(rig.session.project().vector_objects.size(), shapesBefore + 3);
    std::uint64_t copyLayer = 0;
    for (const auto& layer : rig.editor.layers()) {
        const auto map = layer.toMap();
        if (map.value(QStringLiteral("name")).toString() == QStringLiteral("Fond (copie)")) {
            copyLayer = map.value(QStringLiteral("id")).toULongLong();
        }
    }
    QVERIFY(copyLayer != 0);
    int inCopy = 0;
    for (const auto& shape : rig.session.project().vector_objects) {
        inCopy += shape.calque.value == copyLayer ? 1 : 0;
    }
    QCOMPARE(inCopy, 3);
    // La source est intacte.
    QCOMPARE(shapeOf(rig.session.project(), kLone).calque.value, std::uint64_t{201});

    QVERIFY(rig.session.undo());
    QCOMPARE(steps(rig), base);
    QCOMPARE(rig.editor.layers().size(), layersBefore);
    QCOMPARE(rig.session.project().embroidery_objects.size(), objectsBefore);
    QCOMPARE(rig.session.project().vector_objects.size(), shapesBefore);
}

// Rotation autour d'un centre choisi : la ligne tourne autour du point donné (et non de son propre
// centre), en une étape d'annulation ; angle ou centre invalide refusé.
void StudioEditTest::rotationAroundAChosenCentreIsOneUndoStep() {
    Rig rig;
    open(rig);
    const auto base = steps(rig);
    select(rig, {kLone}); // ligne horizontale de x = 40 à 50, y = 25 (mm modèle)
    const auto centre = rig.editor.selectionCentreMm();
    QVERIFY(std::abs(centre.value("x").toDouble() - 45.0) < 0.05);
    QVERIFY(std::abs(centre.value("y").toDouble() - 25.0) < 0.05);
    // 90° anti-horaires autour de l'origine : (x, y) -> (-y, x). Le centre (45, 25) va en (-25,
    // 45).
    QVERIFY(rig.editor.rotateAround(90.0, 0.0, 0.0));
    QCOMPARE(steps(rig), base + 1);
    settle(rig); // la boîte de sélection vient de la scène recalculée
    const auto moved = rig.editor.selectionCentreMm();
    QVERIFY(std::abs(moved.value("x").toDouble() - (-25.0)) < 0.1);
    QVERIFY(std::abs(moved.value("y").toDouble() - 45.0) < 0.1);
    QVERIFY(rig.session.undo());
    QCOMPARE(steps(rig), base);
    settle(rig);
    const auto back = rig.editor.selectionCentreMm();
    QVERIFY(std::abs(back.value("x").toDouble() - 45.0) < 0.05);

    QVERIFY(!rig.editor.rotateAround(400.0, 0.0, 0.0));
    QVERIFY(!rig.editor.rotateAround(std::nan(""), 0.0, 0.0));
    QVERIFY(!rig.editor.rotateAround(10.0, std::nan(""), 0.0));
    QCOMPARE(steps(rig), base);
    QVERIFY(rig.window->findChild<QQuickItem*>(QStringLiteral("pivotApply")));
    QVERIFY(rig.window->findChild<QQuickItem*>(QStringLiteral("pivotX")));
}

void StudioEditTest::rectangleSelectionByMouse() {
    Rig rig;
    open(rig);
    const auto before = steps(rig);
    // De gauche à droite : fenêtre. Elle enferme entièrement la ligne verte (y = -10 mm).
    dragMouse(rig.window, at(rig, -20, -12), at(rig, 20, -8));
    QTRY_COMPARE(selected(rig), (std::vector<std::uint64_t>{kGreen}));
    // De droite à gauche : capture. Elle ne touche que la moitié de la ligne rouge du haut.
    dragMouse(rig.window, at(rig, 8, 12), at(rig, 0, 9));
    QTRY_COMPARE(selected(rig), (std::vector<std::uint64_t>{kRed1, kBlue}));
    // Une fenêtre qui ne contient pas entièrement l'objet ne le prend pas.
    dragMouse(rig.window, at(rig, -3, 12), at(rig, 5, 8));
    QTRY_VERIFY(rig.session.selection().empty());
    // Ctrl ajoute à la sélection existante.
    dragMouse(rig.window, at(rig, -20, -12), at(rig, 20, -8));
    QTRY_COMPARE(selected(rig).size(), std::size_t{1});
    dragMouse(rig.window, at(rig, 18, 3), at(rig, 32, -3), Qt::ControlModifier);
    QTRY_COMPARE(selected(rig), (std::vector<std::uint64_t>{kGreen, kRed2}));
    QCOMPARE(steps(rig), before); // la sélection n'entre pas dans l'historique
}

void StudioEditTest::savedSelectionsAreRecalled() {
    Rig rig;
    open(rig);
    QVERIFY(!rig.editor.saveSelection(QStringLiteral("vide"))); // rien à mémoriser
    select(rig, {kRed1, kRed2});
    QVERIFY(rig.editor.saveSelection(QStringLiteral("rouges")));
    QCOMPARE(rig.editor.savedSelections(), QStringList{QStringLiteral("rouges")});
    rig.editor.selectNone();
    QVERIFY(rig.editor.recallSelection(QStringLiteral("rouges")));
    QCOMPARE(selected(rig), (std::vector<std::uint64_t>{kRed1, kRed2}));
    QVERIFY(!rig.editor.recallSelection(QStringLiteral("absente")));
    // Un objet supprimé disparaît du rappel ; un autre document vide la mémoire.
    select(rig, {kRed2});
    QVERIFY(rig.editor.deleteSelection());
    QVERIFY(rig.editor.recallSelection(QStringLiteral("rouges")));
    QCOMPARE(selected(rig), (std::vector<std::uint64_t>{kRed1}));
    QVERIFY(rig.editor.deleteSavedSelection(QStringLiteral("rouges")));
    QVERIFY(rig.editor.savedSelections().isEmpty());
}

void StudioEditTest::transformsAreOneUndoStepAndPersist() {
    Rig rig;
    open(rig);
    const auto pristine = rig.session.snapshot();
    const auto sequence = stitch_generation::effective_sequence(*pristine);
    QVERIFY(sequence.has_value());
    const auto dstBefore = formats::encode_dst(*sequence);
    QVERIFY(dstBefore.has_value());
    const auto base = steps(rig);

    // Rotation de 90° (horaire) : la ligne horizontale devient verticale, autour de son centre.
    select(rig, {kRed1});
    settle(rig);
    QVERIFY(rig.editor.rotate90(true));
    QCOMPARE(steps(rig), base + 1);
    settle(rig);
    const auto& rotated = rig.session.project();
    QVERIFY(std::abs(nodeX(rotated, kRed1, 0) - nodeX(rotated, kRed1, 1)) < 0.02);
    QVERIFY(std::abs(std::abs(nodeY(rotated, kRed1, 0) - nodeY(rotated, kRed1, 1)) - 30.0) < 0.05);
    QCOMPARE(rig.session.selection().targets().size(), std::size_t{1}); // la sélection survit

    // Miroir horizontal d'une ligne asymétrique (verte du bas) : ses extrémités s'échangent.
    select(rig, {kRed2});
    settle(rig);
    const double leftBefore = nodeX(rig.session.project(), kRed2, 0);
    QVERIFY(rig.editor.mirror(true));
    QCOMPARE(steps(rig), base + 2);
    QVERIFY(std::abs(nodeX(rig.session.project(), kRed2, 0) - (50.0 - leftBefore)) < 0.05);

    // Duplication : une copie décalée devient la sélection.
    select(rig, {kGreen});
    settle(rig);
    const auto count = rig.session.project().embroidery_objects.size();
    QVERIFY(rig.editor.duplicate(5));
    QCOMPARE(steps(rig), base + 3);
    QCOMPARE(rig.session.project().embroidery_objects.size(), count + 1);
    QCOMPARE(rig.session.selection().targets().size(), std::size_t{1});
    QVERIFY(rig.session.selection().targets().front().id != kGreen);

    // Suppression de la copie, puis de l'original.
    QVERIFY(rig.editor.deleteSelection());
    QCOMPARE(rig.session.project().embroidery_objects.size(), count);
    QCOMPARE(steps(rig), base + 4);

    // Persistance : enregistrer puis rouvrir redonne exactement le même document.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = std::filesystem::path(directory.path().toStdWString()) / "edit.osp";
    QVERIFY(project_io::save_project(path, rig.session.project()).has_value());
    const auto reopened = project_io::load_project(path);
    QVERIFY(reopened.has_value());
    for (const auto id : {kRed1, kRed2, kGreen}) {
        QCOMPARE(nodeX(*reopened, id, 0), nodeX(rig.session.project(), id, 0));
        QCOMPARE(nodeY(*reopened, id, 1), nodeY(rig.session.project(), id, 1));
    }

    // Tout annuler redonne la séquence de points d'origine, octet pour octet.
    for (int i = 0; i < 4; ++i) {
        QVERIFY(rig.session.undo());
    }
    settle(rig);
    const auto after = stitch_generation::effective_sequence(*rig.session.snapshot());
    QVERIFY(after.has_value());
    const auto dstAfter = formats::encode_dst(*after);
    QVERIFY(dstAfter.has_value());
    QCOMPARE(dstAfter->size(), dstBefore->size());
    QVERIFY(std::equal(dstAfter->begin(), dstAfter->end(), dstBefore->begin()));
}

void StudioEditTest::invalidTransformsAreRefused() {
    Rig rig;
    open(rig);
    const auto base = steps(rig);
    QVERIFY(!rig.editor.rotate90(true)); // rien de sélectionné
    QVERIFY(!rig.editor.deleteSelection());
    QVERIFY(!rig.editor.duplicate(5));
    select(rig, {kRed1});
    QVERIFY(!rig.editor.scale(100.0, 0, 0)); // hors limites : refusé, jamais corrigé en silence
    QVERIFY(!rig.editor.rotate(1000.0));
    QVERIFY(!rig.editor.align(QStringLiteral("left"))); // un seul objet
    QVERIFY(!rig.editor.distribute(true));
    QVERIFY(!rig.editor.align(QStringLiteral("inconnu")));
    QVERIFY(!rig.editor.stack(QStringLiteral("sideways")));
    QVERIFY(!rig.editor.renameObject(QStringLiteral("999999"), QStringLiteral("x")));
    QVERIFY(!rig.editor.renameObject(QString::number(kRed1), QStringLiteral("  ")));
    QCOMPARE(steps(rig), base);
    QVERIFY(!rig.editor.message().isEmpty());
}

void StudioEditTest::placementInMillimetres() {
    Rig rig;
    open(rig);
    const auto base = steps(rig);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    // Un objet : centre et largeur exacts (mm modèle).
    select(rig, {kRed2});
    settle(rig);
    QVERIFY(rig.editor.placeSelection(100.0, 20.0, 40.0, nan, 0.0));
    QCOMPARE(steps(rig), base + 1);
    const auto& p = rig.session.project();
    QVERIFY(std::abs((nodeX(p, kRed2, 0) + nodeX(p, kRed2, 1)) / 2 - 100.0) < 0.05);
    QVERIFY(std::abs(nodeY(p, kRed2, 0) - 20.0) < 0.05);
    QVERIFY(std::abs(std::abs(nodeX(p, kRed2, 1) - nodeX(p, kRed2, 0)) - 40.0) < 0.1);
    // Plusieurs objets : l'ensemble est déplacé, une seule étape.
    select(rig, {kRed1, kGreen});
    settle(rig);
    const double y1 = nodeY(rig.session.project(), kRed1, 0);
    const double y2 = nodeY(rig.session.project(), kGreen, 0);
    QVERIFY(rig.editor.placeSelection(nan, 50.0, nan, nan, 0.0));
    QCOMPARE(steps(rig), base + 2);
    const auto& q = rig.session.project();
    QVERIFY(std::abs((nodeY(q, kRed1, 0) - y1) - (nodeY(q, kGreen, 0) - y2)) < 0.02);
    QVERIFY(std::abs((nodeY(q, kRed1, 0) + nodeY(q, kGreen, 0)) / 2 - 50.0) < 0.1);
    QVERIFY(!rig.editor.placeSelection(5000.0, nan, nan, nan, 0.0)); // hors limites
    QCOMPARE(steps(rig), base + 2);
}

void StudioEditTest::alignDistributeStackGroup() {
    Rig rig;
    open(rig);
    const auto base = steps(rig);
    select(rig, {kRed1, kRed2});
    settle(rig);
    QVERIFY(rig.editor.align(QStringLiteral("left")));
    QCOMPARE(steps(rig), base + 1);
    {
        const auto& p = rig.session.project();
        QVERIFY(std::abs(std::min(nodeX(p, kRed2, 0), nodeX(p, kRed2, 1)) - (-15.0)) < 0.05);
    }
    select(rig, {kRed1, kGreen, kLone});
    settle(rig);
    QVERIFY(rig.editor.distribute(false)); // trois niveaux de Y inégaux : répartis
    QCOMPARE(steps(rig), base + 2);

    // Ordre de couture : « devant » place l'objet à la fin de la séquence.
    select(rig, {kRed1});
    settle(rig);
    QVERIFY(rig.editor.stack(QStringLiteral("front")));
    QCOMPARE(steps(rig), base + 3);
    QCOMPARE(rig.session.project().embroidery_objects.back().id.value, kRed1);
    QVERIFY(rig.editor.stack(QStringLiteral("back")));
    QCOMPARE(rig.session.project().embroidery_objects.front().id.value, kRed1);

    // Groupe : l'arbre expose un nœud de groupe ; dégrouper le dissout, chacun en une étape.
    select(rig, {kGreen, kBlue});
    settle(rig);
    const auto beforeGroup = steps(rig);
    QVERIFY(rig.editor.group(QStringLiteral("Paire")));
    QCOMPARE(steps(rig), beforeGroup + 1);
    bool foundGroup = false;
    for (const auto& row : rig.editor.tree()) {
        foundGroup =
            foundGroup || (row.toMap().value(QStringLiteral("kind")) == QStringLiteral("group") &&
                           row.toMap().value(QStringLiteral("label")) == QStringLiteral("Paire"));
    }
    QVERIFY(foundGroup);
    QCOMPARE(rig.editor.selectGroup(QString::number(rig.session.project().groups.front().id.value)),
             2);
    QVERIFY(rig.editor.ungroup());
    QVERIFY(rig.session.project().groups.empty());
    QCOMPARE(steps(rig), beforeGroup + 2);
    rig.session.undo();
    rig.session.undo();
    QCOMPARE(steps(rig), beforeGroup);
}

void StudioEditTest::mouseMoveWithSnapGuidesAndUndo() {
    Rig rig;
    open(rig);
    rig.editor.setSnapEnabled(false);
    const double scale = rig.canvas->zoom();
    const auto base = steps(rig);
    // Un clic sélectionne, puis le glisser déplace tout l'objet : une seule commande.
    QTest::mouseClick(rig.window, Qt::LeftButton, Qt::NoModifier, at(rig, -10, 10));
    QTRY_COMPARE(selected(rig), (std::vector<std::uint64_t>{kRed1}));
    const double x0 = nodeX(rig.session.project(), kRed1, 0);
    const double y0 = nodeY(rig.session.project(), kRed1, 0);
    // Mi-parcours : le fantôme est visible et le document n'a pas bougé.
    const auto from = at(rig, -10, 10);
    QTest::mousePress(rig.window, Qt::LeftButton, Qt::NoModifier, from);
    for (int step = 1; step <= 4; ++step) {
        QTest::mouseMove(rig.window, from + QPoint(10 * step, 5 * step));
    }
    auto* ghost = rig.window->findChild<QQuickItem*>(QStringLiteral("ghostRect"));
    QVERIFY(ghost);
    QTRY_VERIFY(ghost->isVisible());
    QCOMPARE(nodeX(rig.session.project(), kRed1, 0), x0);
    QCOMPARE(steps(rig), base);
    QTest::mouseRelease(rig.window, Qt::LeftButton, Qt::NoModifier, from + QPoint(40, 20));
    QCOMPARE(steps(rig), base + 1);
    QTRY_VERIFY(!ghost->isVisible());
    QVERIFY(std::abs((nodeX(rig.session.project(), kRed1, 0) - x0) - 40.0 / scale) < 0.02);
    QVERIFY(std::abs((nodeY(rig.session.project(), kRed1, 0) - y0) - (-20.0 / scale)) < 0.02);
    rig.session.undo();
    QCOMPARE(nodeX(rig.session.project(), kRed1, 0), x0);

    // Accrochage : près de l'alignement avec la ligne verte (mêmes bords), un petit écart se
    // corrige, les repères sont tracés pendant le glisser et rien n'est écrit si le décalage est
    // nul.
    rig.editor.setSnapEnabled(true);
    settle(rig);
    const auto start = at(rig, -10, 10);
    QTest::mousePress(rig.window, Qt::LeftButton, Qt::NoModifier, start);
    for (int step = 1; step <= 3; ++step) {
        QTest::mouseMove(rig.window, start + QPoint(step, 0)); // 3 px : sous le seuil d'accrochage
    }
    QTRY_VERIFY(!rig.canvas->guideLines().isEmpty());
    QTest::mouseRelease(rig.window, Qt::LeftButton, Qt::NoModifier, start + QPoint(3, 0));
    QCOMPARE(nodeX(rig.session.project(), kRed1, 0), x0); // accroché : aucun décalage résiduel
    // Échap pendant un glisser annule sans toucher au document.
    const auto before = steps(rig);
    QTest::mousePress(rig.window, Qt::LeftButton, Qt::NoModifier, start);
    for (int step = 1; step <= 4; ++step) {
        QTest::mouseMove(rig.window, start + QPoint(15 * step, 0));
    }
    QTest::keyClick(rig.window, Qt::Key_Escape);
    QTest::mouseRelease(rig.window, Qt::LeftButton, Qt::NoModifier, start + QPoint(60, 0));
    QCOMPARE(steps(rig), before);
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioEditTest::handlesResizeAndRotate() {
    Rig rig;
    open(rig);
    select(rig, {kRed2});
    settle(rig);
    const auto base = steps(rig);
    QTRY_VERIFY(rig.canvas->selectionRect().width() > 10.0);
    auto* handle = deepItem(rig.window->contentItem(), QStringLiteral("handle4"));
    QVERIFY(handle); // poignée sud-est, dessinée par TransformHandles.qml
    const auto box = rig.canvas->selectionRect();
    const double spanBefore =
        nodeX(rig.session.project(), kRed2, 1) - nodeX(rig.session.project(), kRed2, 0);
    const auto corner = windowPoint(*rig.canvas, {box.right(), box.bottom()});
    dragMouse(rig.window, corner, corner + QPoint(static_cast<int>(box.width()), 0));
    QCOMPARE(steps(rig), base + 1);
    const double spanAfter =
        nodeX(rig.session.project(), kRed2, 1) - nodeX(rig.session.project(), kRed2, 0);
    QVERIFY(std::abs(spanAfter / spanBefore - 2.0) < 0.1);
    // L'ancre (coin opposé) reste en place : le bord gauche n'a pas bougé.
    QVERIFY(std::abs(nodeX(rig.session.project(), kRed2, 0) - 20.0) < 0.2);

    // Rotation à la poignée : un quart de tour horaire redresse la ligne.
    settle(rig); // la boîte de la sélection suit la nouvelle géométrie
    QTRY_VERIFY(rig.canvas->selectionRect().width() > 10.0);
    const auto rect = rig.canvas->selectionRect();
    const auto knob = windowPoint(*rig.canvas, {rect.center().x(), rect.top() - 26.0});
    const auto centre = windowPoint(*rig.canvas, {rect.center().x(), rect.center().y()});
    const auto radius = centre.y() - knob.y();
    dragMouse(rig.window, knob, centre + QPoint(radius, 0), Qt::ShiftModifier);
    QCOMPARE(steps(rig), base + 2);
    const auto& p = rig.session.project();
    QVERIFY2(std::abs(nodeX(p, kRed2, 0) - nodeX(p, kRed2, 1)) < 0.2 &&
                 std::abs(nodeY(p, kRed2, 0) - nodeY(p, kRed2, 1)) > 19.0,
             qPrintable(QStringLiteral("x0=%1 x1=%2 y0=%3 y1=%4 knob=%5,%6 centre=%7,%8")
                            .arg(nodeX(p, kRed2, 0))
                            .arg(nodeX(p, kRed2, 1))
                            .arg(nodeY(p, kRed2, 0))
                            .arg(nodeY(p, kRed2, 1))
                            .arg(knob.x())
                            .arg(knob.y())
                            .arg(centre.x())
                            .arg(centre.y())));
    rig.session.undo();
    rig.session.undo();
    QCOMPARE(steps(rig), base);
}

void StudioEditTest::layersTreeAndObjectEditing() {
    Rig rig;
    open(rig);
    const auto base = steps(rig);
    // L'arbre : deux calques (le dessus d'abord) et cinq objets.
    const auto rows = rig.editor.tree();
    QCOMPARE(rows.size(), 7);
    QCOMPARE(rows.front().toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("layer"));
    QCOMPARE(rows.front().toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("Dessus"));
    // Le même arbre est affiché par le QML (mode « Arbre et calques »).
    rig.window->resize(1440,
                       1300); // les sept lignes tiennent sans défilement : mise en page stable
    QTRY_COMPARE(rig.window->height(), 1300);
    auto* treeButton = rig.window->findChild<QQuickItem*>(QStringLiteral("treeModeButton"));
    QVERIFY(treeButton);
    QTest::mouseClick(
        rig.window, Qt::LeftButton, Qt::NoModifier,
        treeButton->mapToScene(QPointF(treeButton->width() / 2, treeButton->height() / 2))
            .toPoint());
    auto* list = rig.window->findChild<QQuickItem*>(QStringLiteral("objectTreeList"));
    QVERIFY(list);
    QTRY_COMPARE(list->property("count").toInt(), 7);

    // Glisser-déposer à la souris : la ligne d'un objet déposée sur une ligne de calque l'y
    // déplace.
    {
        QQuickItem *objectRow = nullptr, *layerRow = nullptr;
        for (int i = 0; i < 7; ++i) {
            QQuickItem* item = nullptr;
            QTRY_VERIFY(QMetaObject::invokeMethod(list, "itemAtIndex",
                                                  Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, i)) &&
                        item);
            const auto kind = item->property("modelData").toMap().value(QStringLiteral("kind"));
            const auto id = item->property("modelData").toMap().value(QStringLiteral("id"));
            if (kind == QStringLiteral("object") && id == QString::number(kLone)) {
                objectRow = item;
            } else if (kind == QStringLiteral("layer") && id == QStringLiteral("202")) {
                layerRow = item;
            }
        }
        QVERIFY(objectRow && layerRow);
        const auto from = objectRow->mapToScene(QPointF(40, objectRow->height() / 2)).toPoint();
        const auto to = layerRow->mapToScene(QPointF(40, layerRow->height() / 2)).toPoint();
        const auto stepsBeforeDrop = steps(rig);
        QVERIFY(objectRow->y() + objectRow->height() / 2 < list->height()); // ligne visible
        dragMouse(rig.window, from, to);
        QTRY_COMPARE(shapeOf(rig.session.project(), kLone).calque.value, std::uint64_t{202});
        QCOMPARE(steps(rig), stepsBeforeDrop + 1);
        QVERIFY(rig.session.undo());
        QCOMPARE(shapeOf(rig.session.project(), kLone).calque.value, std::uint64_t{201});
        settle(rig);
    }

    // Calques : créer, renommer, déplacer, masquer, verrouiller, supprimer (une étape chacun).
    QVERIFY(rig.editor.addLayer(QStringLiteral("Nouveau")));
    QCOMPARE(steps(rig), base + 1);
    QCOMPARE(rig.editor.layers().size(), 3);
    const auto layerId = rig.editor.layers().front().toMap().value(QStringLiteral("id")).toString();
    QVERIFY(rig.editor.renameLayer(layerId, QStringLiteral("Broderie")));
    QCOMPARE(rig.editor.layers().front().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Broderie"));
    QVERIFY(rig.editor.moveLayer(layerId, -1));
    QVERIFY(rig.editor.setLayerLocked(layerId, true));
    QVERIFY(rig.editor.setLayerLocked(layerId, false));
    // Déplacer la sélection vers ce calque.
    select(rig, {kBlue, kLone});
    QVERIFY(rig.editor.moveSelectionToLayer(layerId));
    const auto stepsAfterMove = steps(rig);
    QCOMPARE(shapeOf(rig.session.project(), kBlue).calque.value, layerId.toULongLong());
    QCOMPARE(shapeOf(rig.session.project(), kLone).calque.value, layerId.toULongLong());
    // Masquer un calque masque ses objets dans la liste (et à l'écran).
    QVERIFY(rig.editor.setLayerVisible(layerId, false));
    settle(rig);
    for (const auto& entry : rig.controller.objects()) {
        const auto map = entry.toMap();
        const auto id = map.value(QStringLiteral("id")).toULongLong();
        QCOMPARE(map.value(QStringLiteral("visible")).toBool(), id != kBlue && id != kLone);
    }
    QCOMPARE(rig.editor.selectAll(), 3);
    QVERIFY(rig.editor.setLayerVisible(layerId, true));
    // Un calque verrouillé protège ses objets de toute transformation.
    QVERIFY(rig.editor.setLayerLocked(layerId, true));
    select(rig, {kBlue});
    QVERIFY(!rig.editor.rotate90(true));
    QVERIFY(rig.editor.setLayerLocked(layerId, false));
    // Supprimer le calque : ses formes retombent dans le calque du fond ; annulable d'un coup.
    const auto beforeRemove = steps(rig);
    QVERIFY(rig.editor.removeLayer(layerId));
    QCOMPARE(steps(rig), beforeRemove + 1);
    QVERIFY(rig.session.undo());
    QCOMPARE(rig.editor.layers().size(), 3);
    QVERIFY(stepsAfterMove > base);

    // Renommer un objet : annulable, enregistré, rouvert.
    QVERIFY(rig.editor.renameObject(QString::number(kGreen), QStringLiteral("Vert nommé")));
    QCOMPARE(QString::fromStdString(rig.session.project().findEmbroidery(ObjectId{kGreen})->name),
             QStringLiteral("Vert nommé"));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = std::filesystem::path(directory.path().toStdWString()) / "layers.osp";
    QVERIFY(project_io::save_project(path, rig.session.project()).has_value());
    const auto reopened = project_io::load_project(path);
    QVERIFY(reopened.has_value());
    QCOMPARE(reopened->findEmbroidery(ObjectId{kGreen})->name, std::string("Vert nommé"));
    QCOMPARE(reopened->calques.size(), std::size_t{3});
    QCOMPARE(shapeOf(*reopened, kBlue).calque.value, layerId.toULongLong());
    rig.session.undo();
    QCOMPARE(rig.session.project().findEmbroidery(ObjectId{kGreen})->name,
             std::string("Ligne 102"));
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioEditTest::contextMenuAndEyedropper() {
    Rig rig;
    open(rig);
    QSignalSpy requested(rig.canvas, &GpuCanvas::contextMenuRequested);
    QTest::mouseClick(rig.window, Qt::RightButton, Qt::NoModifier, at(rig, -10, -10));
    QCOMPARE(requested.size(), 1);
    QCOMPARE(requested.front().at(2).toString(), QString::number(kGreen));
    QTRY_COMPARE(selected(rig), (std::vector<std::uint64_t>{kGreen})); // le clic droit sélectionne
    auto* menu = rig.window->findChild<QObject*>(QStringLiteral("canvasContextMenu"));
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("visible").toBool());
    auto* remove = rig.window->findChild<QObject*>(QStringLiteral("contextDelete"));
    QVERIFY(remove);
    QVERIFY(remove->property("text").toString().contains(QStringLiteral("Supprimer 1")));
    const auto count = rig.session.project().embroidery_objects.size();
    QVERIFY(QMetaObject::invokeMethod(remove, "triggered"));
    QCOMPARE(rig.session.project().embroidery_objects.size(), count - 1);
    QVERIFY(rig.session.undo());
    QCOMPARE(rig.session.project().embroidery_objects.size(), count);
    QMetaObject::invokeMethod(menu, "close");

    // Pipette : la couleur de l'objet cliqué passe à la sélection, en une étape.
    settle(rig);
    select(rig, {kGreen});
    rig.editor.setEyedropper(true);
    const auto base = steps(rig);
    QTest::mouseClick(rig.window, Qt::LeftButton, Qt::NoModifier, at(rig, -10, 10)); // ligne rouge
    QTRY_COMPARE(steps(rig), base + 1);
    QCOMPARE(rig.session.project().findEmbroidery(ObjectId{kGreen})->rgb,
             (std::array<std::uint8_t, 3>{200, 20, 30}));
    QCOMPARE(selected(rig), (std::vector<std::uint64_t>{kGreen})); // la sélection ne change pas
    rig.session.undo();
    QCOMPARE(rig.session.project().findEmbroidery(ObjectId{kGreen})->rgb,
             (std::array<std::uint8_t, 3>{20, 180, 60}));
    rig.editor.setEyedropper(false);
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioEditTest::shortcutsReachTheEditor() {
    Rig rig;
    open(rig);
    rig.canvas->forceActiveFocus();
    QTest::keyClick(rig.window, Qt::Key_A, Qt::ControlModifier);
    QTRY_COMPARE(selected(rig).size(), std::size_t{5});
    QTest::keyClick(rig.window, Qt::Key_Escape);
    QTRY_VERIFY(rig.session.selection().empty());
    select(rig, {kRed1});
    settle(rig);
    const auto base = steps(rig);
    const auto count = rig.session.project().embroidery_objects.size();
    QTest::keyClick(rig.window, Qt::Key_D, Qt::ControlModifier);
    QTRY_COMPARE(rig.session.project().embroidery_objects.size(), count + 1);
    QTest::keyClick(rig.window, Qt::Key_Delete);
    QTRY_COMPARE(rig.session.project().embroidery_objects.size(), count);
    QCOMPARE(steps(rig), base + 2);
    QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(rig.window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE(steps(rig), base);
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioEditTest::focusedFieldOwnsArrowKeysDeleteAndEscape() {
    // QA-STUDIO-001 KEY-008/KEY-010/CTL-018/019: with a field focused, the global canvas
    // shortcuts (nudge, delete, clear selection) must not fire behind it.
    Rig rig;
    open(rig);
    select(rig, {kRed1});
    settle(rig);
    QQmlComponent component(&rig.engine);
    component.setData(R"qml(
        import QtQuick
        TextInput { objectName: "qaField"; width: 80; height: 20 }
    )qml",
                      QUrl());
    auto* field = qobject_cast<QQuickItem*>(component.create());
    QVERIFY2(field, qPrintable(component.errorString()));
    field->setParentItem(rig.window->contentItem());
    field->forceActiveFocus();
    QTRY_VERIFY(rig.window->activeFocusItem() == field);

    const auto revision = rig.session.revision();
    const auto count = rig.session.project().embroidery_objects.size();
    for (const auto key : {Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right, Qt::Key_Home,
                           Qt::Key_End, Qt::Key_PageUp, Qt::Key_PageDown, Qt::Key_Delete}) {
        QTest::keyClick(rig.window, key);
        QTest::keyClick(rig.window, key, Qt::ShiftModifier);
    }
    QTest::qWait(100);
    QCOMPARE(rig.session.revision(), revision);
    QCOMPARE(rig.session.project().embroidery_objects.size(), count);
    QCOMPARE(selected(rig), (std::vector<std::uint64_t>{kRed1}));

    QTest::keyClick(rig.window, Qt::Key_Escape); // leaves the field, keeps the selection
    QTest::qWait(100);
    QCOMPARE(selected(rig), (std::vector<std::uint64_t>{kRed1}));
    QVERIFY(rig.window->activeFocusItem() != field);

    QTest::keyClick(rig.window, Qt::Key_Up); // focus released: the nudge works again
    QTRY_VERIFY(rig.session.revision() > revision);
    delete field;
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join(QLatin1Char('\n'))));
}

QTEST_MAIN(StudioEditTest)
#include "test_studio_edit.moc"
