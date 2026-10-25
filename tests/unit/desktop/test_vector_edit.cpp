// SPDX-License-Identifier: Apache-2.0
// Édition vectorielle (HP-VEC-001/005/006) : double-clic sur le contour = nouveau nœud, Alt +
// double-clic dans un trou = suppression de CE trou, Forme > Contour autour de la sélection.
// Headless, sans pixel ni sleep ; chaque geste = un pas d'annulation.
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

geo::Path box(int x0, int x1, int y0, int y1) {
    geo::Path path;
    path.closed = true;
    for (const auto& [x, y] : {std::pair{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}) {
        path.nodes.push_back({Vec2um{Micrometers{x}, Micrometers{y}}, geo::NodeType::Corner,
                              std::nullopt, std::nullopt});
    }
    return path;
}

// Carré 30 mm centré, avec un trou 6 mm centré.
doc::Project squareWithHole(ObjectId& id) {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    doc::VectorObject object;
    object.id = project.object_ids.next();
    object.name = "Carre";
    object.paths.push_back(
        {box(-15'000, 15'000, -15'000, 15'000), {box(-3'000, 3'000, -3'000, 3'000)}});
    id = object.id;
    project.vector_objects.push_back(std::move(object));
    return project;
}

} // namespace

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    struct Fixture {
        MainWindow window;
        ObjectId id;
    };

    static void setup(Fixture& f) {
        f.window.resize(1200, 800);
        f.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&f.window));
        f.window.applyLoadedProject(squareWithHole(f.id));
        f.window.setSelection(
            {.region = std::nullopt, .embroidery = std::nullopt, .objects = {f.id}});
        f.window.setTool(Tool::Select);
        f.window.refreshImage();
        auto* view = f.window.findChild<CanvasView*>();
        QVERIFY(view != nullptr);
        view->resetTransform();
        view->scale(10.0, 10.0);
        view->centerOn(QPointF(0.0, 0.0));
        QCoreApplication::processEvents();
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("VectorEditTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void doubleClickOnTheOutlineInsertsOneNodeInOneUndoStep() {
        Fixture f;
        setup(f);
        const auto before = f.window.project_.findObject(f.id)->paths;
        // Bord bas : y modèle = -15 mm -> scène +15 mm.
        f.window.onCanvasDoubleClicked(QPointF(4.0, 15.0));
        const auto& nodes = f.window.project_.findObject(f.id)->paths.front().outer.nodes;
        QCOMPARE(nodes.size(), size_t{5});
        QCOMPARE(nodes[1].pos.y.value, -15'000); // sur le bord, forme inchangée
        QVERIFY(f.window.undoStack_.undo(f.window.project_));
        QVERIFY(f.window.project_.findObject(f.id)->paths == before);
        QVERIFY(!f.window.undoStack_.canUndo());
    }

    void doubleClickAwayFromTheOutlineChangesNothing() {
        Fixture f;
        setup(f);
        f.window.onCanvasDoubleClicked(QPointF(8.0, 8.0));
        QVERIFY(!f.window.undoStack_.canUndo());
    }

    void altDoubleClickInsideTheHoleRemovesIt() {
        Fixture f;
        setup(f);
        QVERIFY(f.window.removeHoleAt(QPointF(0.0, 0.0)));
        QVERIFY(f.window.project_.findObject(f.id)->paths.front().holes.empty());
        QVERIFY(f.window.undoStack_.undo(f.window.project_));
        QCOMPARE(f.window.project_.findObject(f.id)->paths.front().holes.size(), size_t{1});
        // Hors du trou : refusé, rien ne change.
        QVERIFY(!f.window.removeHoleAt(QPointF(10.0, 10.0)));
    }

    void gridSnapRoundsToTheGridOnlyWhenEnabled() {
        Fixture f;
        setup(f);
        f.window.gridStepMm_ = 2.0;
        const QPointF cursor(6.3, 0.1);
        QVERIFY(f.window.gridSnapAct_ != nullptr);
        f.window.gridSnapAct_->setChecked(false);
        QVERIFY(!f.window.findSnapPointMm(cursor).has_value());
        f.window.gridSnapAct_->setChecked(true);
        const auto snapped = f.window.findSnapPointMm(cursor);
        QVERIFY(snapped.has_value());
        QCOMPARE(snapped->x(), 6.0);
        QCOMPARE(snapped->y(), 0.0);
        // Trop loin d'un point de grille (pas 20 mm, curseur à 10 mm) : pas d'accroche.
        f.window.gridStepMm_ = 20.0;
        QVERIFY(!f.window.findSnapPointMm(QPointF(10.0, 10.0)).has_value());
        f.window.gridSnapAct_->setChecked(false);
    }

    void outlineActionCreatesAnOutlineObjectInOneUndoStep() {
        Fixture f;
        setup(f);
        QVERIFY(f.window.outlineAct_ != nullptr);
        f.window.updateActions();
        QVERIFY(f.window.outlineAct_->isEnabled());
        f.window.outlineSelection(0, 1.0, 2.5, false);
        QCOMPARE(f.window.project_.vector_objects.size(), size_t{2});
        QCOMPARE(f.window.project_.embroidery_objects.size(), size_t{1});
        QVERIFY(f.window.undoStack_.undo(f.window.project_));
        QCOMPARE(f.window.project_.vector_objects.size(), size_t{1});
        QVERIFY(f.window.project_.embroidery_objects.empty());
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_vector_edit.moc"
