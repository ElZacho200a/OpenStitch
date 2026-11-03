// SPDX-License-Identifier: Apache-2.0
// Sélection multiple de nœuds et déplacement groupé (HP-VEC-017) avec de VRAIS gestes souris :
// Alt + cadre sélectionne les nœuds, un clic sur une poignée la sélectionne (Maj ajoute), glisser
// un nœud sélectionné déplace tout le groupe en un seul pas d'annulation, Échap annule.
#include <QAction>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

// Polyligne ouverte de 7 nœuds espacés de 4 mm sur y = 0 (assez dense pour la transition douce).
doc::Project lineProject(ObjectId& first, ObjectId& second) {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    for (const int dy : {0, 60'000}) {
        doc::VectorObject object;
        object.id = project.object_ids.next();
        object.name = "Ligne";
        geo::Path p;
        p.closed = false;
        for (int i = 0; i < 7; ++i) {
            p.nodes.push_back({Vec2um{Micrometers{-12'000 + i * 4'000}, Micrometers{dy}},
                               geo::NodeType::Corner, std::nullopt, std::nullopt});
        }
        object.paths.push_back({p, {}});
        (dy == 0 ? first : second) = object.id;
        project.vector_objects.push_back(std::move(object));
    }
    return project;
}

} // namespace

namespace openstitch::desktop {

// Nom imposé par `friend class MainWindowTest` (main_window.hpp).
class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    struct Fixture {
        MainWindow window;
        ObjectId a;
        ObjectId b;
        CanvasView* view{nullptr};
    };

    static void setup(Fixture& f) {
        f.window.resize(1200, 800);
        f.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&f.window));
        f.window.applyLoadedProject(lineProject(f.a, f.b));
        f.window.setSelection(
            {.region = std::nullopt, .embroidery = std::nullopt, .objects = {f.a}});
        f.window.setTool(Tool::Select);
        f.window.refreshImage();
        f.view = f.window.findChild<CanvasView*>();
        QVERIFY(f.view != nullptr);
        f.view->resetTransform();
        f.view->scale(10.0, 10.0);
        f.view->centerOn(QPointF(0.0, 0.0));
        QCoreApplication::processEvents();
    }

    // Pixel du nœud i de la ligne (modèle y = 0 -> scène y = 0).
    static QPoint nodePx(CanvasView* view, int i) {
        return view->mapFromScene(QPointF(-12.0 + 4.0 * i, 0.0));
    }

    static void altRect(CanvasView* view, QPoint from, QPoint to,
                        Qt::KeyboardModifiers mods = Qt::AltModifier) {
        QTest::mousePress(view->viewport(), Qt::LeftButton, mods, from);
        QTest::mouseMove(view->viewport(), (from + to) / 2);
        QTest::mouseMove(view->viewport(), to);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, mods, to);
        QCoreApplication::processEvents();
    }

    static void dragHandle(CanvasView* view, QPoint from, QPoint to) {
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(view->viewport(), (from + to) / 2);
        QTest::mouseMove(view->viewport(), to);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCoreApplication::processEvents();
    }

    static int posX(Fixture& f, ObjectId id, std::size_t i) {
        return f.window.project_.findObject(id)->paths.front().outer.nodes[i].pos.x.value;
    }
    static int posY(Fixture& f, ObjectId id, std::size_t i) {
        return f.window.project_.findObject(id)->paths.front().outer.nodes[i].pos.y.value;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("NodeSelectionTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void altRectangleSelectsTheNodesInsideAndShiftAdds() {
        Fixture f;
        setup(f);
        // Cadre autour des nœuds 1 et 2 (hors de la ligne : départ au-dessus d'elle).
        altRect(f.view, nodePx(f.view, 1) + QPoint(-12, -30), nodePx(f.view, 2) + QPoint(12, 30));
        QCOMPARE(f.window.selectedNodes_.size(), std::size_t{2});
        QVERIFY(f.window.isNodeSelected({0, 0, 1}));
        QVERIFY(f.window.isNodeSelected({0, 0, 2}));
        // Aucune commande : sélectionner n'est pas modifier.
        QVERIFY(!f.window.undoStack_.canUndo());
        // Alt + Maj ajoute le nœud 4.
        altRect(f.view, nodePx(f.view, 4) + QPoint(-12, -30), nodePx(f.view, 4) + QPoint(12, 30),
                Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(f.window.selectedNodes_.size(), std::size_t{3});
        // Alt + Ctrl bascule : retire le nœud 4.
        altRect(f.view, nodePx(f.view, 4) + QPoint(-12, -30), nodePx(f.view, 4) + QPoint(12, 30),
                Qt::AltModifier | Qt::ControlModifier);
        QCOMPARE(f.window.selectedNodes_.size(), std::size_t{2});
    }

    void clickingAHandleSelectsItAndShiftClickAdds() {
        Fixture f;
        setup(f);
        QTest::mouseClick(f.view->viewport(), Qt::LeftButton, Qt::NoModifier, nodePx(f.view, 3));
        QCoreApplication::processEvents();
        QCOMPARE(f.window.selectedNodes_.size(), std::size_t{1});
        QTest::mouseClick(f.view->viewport(), Qt::LeftButton, Qt::ShiftModifier, nodePx(f.view, 5));
        QCoreApplication::processEvents();
        QCOMPARE(f.window.selectedNodes_.size(), std::size_t{2});
        QVERIFY(!f.window.undoStack_.canUndo()); // un clic ne modifie jamais le document
        QCOMPARE(posX(f, f.a, 3), 0);
    }

    void draggingASelectedNodeMovesTheWholeGroupInOneUndoStep() {
        Fixture f;
        setup(f);
        const auto before = f.window.project_.findObject(f.a)->paths;
        const auto otherBefore = f.window.project_.findObject(f.b)->paths;
        altRect(f.view, nodePx(f.view, 2) + QPoint(-12, -30), nodePx(f.view, 4) + QPoint(12, 30));
        QCOMPARE(f.window.selectedNodes_.size(), std::size_t{3});
        const QPoint from = nodePx(f.view, 3);
        const QPoint to = f.view->mapFromScene(QPointF(0.0, -5.0)); // 5 mm vers le haut
        QTest::mousePress(f.view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(f.view->viewport(), (from + to) / 2);
        QTest::mouseMove(f.view->viewport(), to);
        QVERIFY2(f.window.reshapePreview_ != nullptr, "aperçu du déplacement groupé attendu");
        QVERIFY(!f.window.undoStack_.canUndo()); // rien avant le relâchement
        QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCoreApplication::processEvents();

        for (const std::size_t i : {2u, 3u, 4u}) {
            QCOMPARE(posY(f, f.a, i), 5'000); // groupe monté de 5 mm
        }
        for (const std::size_t i : {0u, 1u, 5u, 6u}) {
            QCOMPARE(posY(f, f.a, i), 0); // les autres nœuds ne bougent pas
        }
        QCOMPARE(f.window.project_.findObject(f.a)->paths.front().outer.nodes.size(),
                 std::size_t{7});
        QCOMPARE(f.window.project_.findObject(f.b)->paths == otherBefore, true);
        QVERIFY(f.window.reshapePreview_ == nullptr);

        const auto after = f.window.project_.findObject(f.a)->paths;
        QVERIFY(f.window.undoStack_.undo(f.window.project_));
        QCOMPARE(f.window.project_.findObject(f.a)->paths == before, true);
        QVERIFY(!f.window.undoStack_.canUndo()); // un seul pas
        QVERIFY(f.window.undoStack_.redo(f.window.project_));
        QCOMPARE(f.window.project_.findObject(f.a)->paths == after, true);
    }

    void softTransitionMakesTheNeighboursFollow() {
        Fixture f;
        setup(f);
        QVERIFY(f.window.softNodesAct_ != nullptr);
        f.window.softNodesAct_->setChecked(true);
        altRect(f.view, nodePx(f.view, 3) + QPoint(-12, -30), nodePx(f.view, 3) + QPoint(12, 30));
        QCOMPARE(f.window.selectedNodes_.size(), std::size_t{1});
        // Un seul nœud sélectionné : on le sélectionne avec un second pour déclencher le groupe.
        QTest::mouseClick(f.view->viewport(), Qt::LeftButton, Qt::ShiftModifier, nodePx(f.view, 4));
        QCoreApplication::processEvents();
        QCOMPARE(f.window.selectedNodes_.size(), std::size_t{2});
        const QPoint from = nodePx(f.view, 3);
        dragHandle(f.view, from, f.view->mapFromScene(QPointF(0.0, -4.0)));
        QCOMPARE(posY(f, f.a, 3), 4'000);
        QCOMPARE(posY(f, f.a, 4), 4'000);
        QVERIFY(posY(f, f.a, 2) > 0); // le voisin suit
        QVERIFY(posY(f, f.a, 2) < 4'000);
        QCOMPARE(posY(f, f.a, 0), 0); // hors du rayon de 8 mm
        QCOMPARE(posY(f, f.a, 6), 0);
    }

    void escapeCancelsTheGroupDragWithoutAnyChange() {
        Fixture f;
        setup(f);
        const auto before = f.window.project_.findObject(f.a)->paths;
        altRect(f.view, nodePx(f.view, 2) + QPoint(-12, -30), nodePx(f.view, 3) + QPoint(12, 30));
        QCOMPARE(f.window.selectedNodes_.size(), std::size_t{2});
        const QPoint from = nodePx(f.view, 2);
        const QPoint to = f.view->mapFromScene(QPointF(-4.0, -5.0));
        QTest::mousePress(f.view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(f.view->viewport(), (from + to) / 2);
        QTest::mouseMove(f.view->viewport(), to);
        QVERIFY(f.window.reshapePreview_ != nullptr);
        QTest::keyClick(f.view->viewport(), Qt::Key_Escape);
        QVERIFY2(f.window.reshapePreview_ == nullptr, "Échap doit retirer l'aperçu");
        QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCoreApplication::processEvents();
        QCOMPARE(f.window.project_.findObject(f.a)->paths == before, true);
        QVERIFY(!f.window.undoStack_.canUndo());
    }

    void changingTheSelectedShapeClearsTheNodeSelection() {
        Fixture f;
        setup(f);
        altRect(f.view, nodePx(f.view, 1) + QPoint(-12, -30), nodePx(f.view, 2) + QPoint(12, 30));
        QCOMPARE(f.window.selectedNodes_.size(), std::size_t{2});
        f.window.setSelection(
            {.region = std::nullopt, .embroidery = std::nullopt, .objects = {f.b}});
        f.window.refreshImage();
        QVERIFY(f.window.selectedNodes_.empty());
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_node_selection.moc"
