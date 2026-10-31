// SPDX-License-Identifier: Apache-2.0
// Pinceau de déformation (HP-VEC-018) avec de VRAIS gestes souris : un coup de pinceau pousse le
// contour de la forme sélectionnée en une seule commande annulable, Échap l'abandonne sans toucher
// au document, Maj lisse, le nombre de nœuds ne dérive pas, les autres objets ne bougent pas.
#include <QAction>
#include <QApplication>
#include <QMouseEvent>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>

#include <cmath>
#include <numbers>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

// Disque polygonal de 40 mm de diamètre (72 nœuds) centré en (cx, 0).
geo::Path disc(int cx) {
    geo::Path p;
    p.closed = true;
    for (int i = 0; i < 72; ++i) {
        const double a = 2.0 * std::numbers::pi * i / 72.0;
        p.nodes.push_back(
            {Vec2um{Micrometers{cx + static_cast<int>(std::lround(20'000 * std::cos(a)))},
                    Micrometers{static_cast<int>(std::lround(20'000 * std::sin(a)))}},
             geo::NodeType::Corner, std::nullopt, std::nullopt});
    }
    return p;
}

doc::Project twoDiscs(ObjectId& first, ObjectId& second) {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    for (const int cx : {0, 90'000}) {
        doc::VectorObject object;
        object.id = project.object_ids.next();
        object.name = "Disque";
        object.paths.push_back({disc(cx), {}});
        (cx == 0 ? first : second) = object.id;
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
        f.window.applyLoadedProject(twoDiscs(f.a, f.b));
        f.window.setSelection(
            {.region = std::nullopt, .embroidery = std::nullopt, .objects = {f.a}});
        f.window.refreshImage();
        f.view = f.window.findChild<CanvasView*>();
        QVERIFY(f.view != nullptr);
        f.view->resetTransform();
        f.view->scale(8.0, 8.0);
        f.view->centerOn(QPointF(0.0, 0.0));
        f.window.setBrushSettings(6.0, 1.0);
        f.window.toolBrushAct_->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(f.window.currentTool_, Tool::Brush);
    }

    static QPoint px(CanvasView* view, double x, double y) {
        return view->mapFromScene(QPointF(x, y));
    }

    static void sendMouse(CanvasView* view, QEvent::Type type, QPoint at, Qt::MouseButton button,
                          Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
        QMouseEvent event(type, QPointF(at), QPointF(view->viewport()->mapToGlobal(at)), button,
                          buttons, mods);
        QApplication::sendEvent(view->viewport(), &event);
    }

    // Glisse de `from` à `to` (scène, mm) en `steps` pas, avec de VRAIS modificateurs (QTest::
    // mouseMove n'en transporte pas).
    static void stroke(CanvasView* view, QPointF from, QPointF to, int steps = 12,
                       Qt::KeyboardModifiers mods = Qt::NoModifier, bool release = true) {
        sendMouse(view, QEvent::MouseButtonPress, px(view, from.x(), from.y()), Qt::LeftButton,
                  Qt::LeftButton, mods);
        for (int i = 1; i <= steps; ++i) {
            const QPointF p = from + (to - from) * (static_cast<double>(i) / steps);
            sendMouse(view, QEvent::MouseMove, px(view, p.x(), p.y()), Qt::NoButton, Qt::LeftButton,
                      mods);
        }
        if (release) {
            sendMouse(view, QEvent::MouseButtonRelease, px(view, to.x(), to.y()), Qt::LeftButton,
                      Qt::NoButton, mods);
            QCoreApplication::processEvents();
        }
    }

    static int maxX(Fixture& f, ObjectId id) {
        int m = -1'000'000;
        for (const auto& n : f.window.project_.findObject(id)->paths.front().outer.nodes) {
            m = std::max(m, n.pos.x.value);
        }
        return m;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("BrushToolTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void theToolIsInThePaletteWithAShortcutAndTheGestureTable() {
        MainWindow window;
        QVERIFY(window.toolBrushAct_ != nullptr);
        QVERIFY(window.toolPalette_->actions().contains(window.toolBrushAct_));
        QCOMPARE(window.toolBrushAct_->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_B));
    }

    void aStrokePushesTheContourInOneUndoStep() {
        Fixture f;
        setup(f);
        const auto before = f.window.project_.findObject(f.a)->paths;
        const auto otherBefore = f.window.project_.findObject(f.b)->paths;
        const int xBefore = maxX(f, f.a);
        // Du bord droit (20, 0) vers l'extérieur : le contour est tiré vers la droite.
        stroke(f.view, QPointF(19.0, 0.0), QPointF(26.0, 0.0), 10, Qt::NoModifier, false);
        QVERIFY2(f.window.reshapePreview_ != nullptr, "aperçu du coup de pinceau attendu");
        QVERIFY(!f.window.undoStack_.canUndo()); // rien avant le relâchement
        QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, Qt::NoModifier,
                            px(f.view, 26.0, 0.0));
        QCoreApplication::processEvents();

        QVERIFY(f.window.project_.findObject(f.a)->paths != before);
        QVERIFY(maxX(f, f.a) > xBefore + 2'000); // le bord a suivi le pinceau
        QCOMPARE(f.window.project_.findObject(f.b)->paths == otherBefore, true);
        QVERIFY(f.window.reshapePreview_ == nullptr);
        QCOMPARE(f.window.undoStack_.undoNames().size(), std::size_t{1}); // un seul pas

        const auto after = f.window.project_.findObject(f.a)->paths;
        QVERIFY(f.window.undoStack_.undo(f.window.project_));
        QCOMPARE(f.window.project_.findObject(f.a)->paths == before, true);
        QVERIFY(f.window.undoStack_.redo(f.window.project_));
        QCOMPARE(f.window.project_.findObject(f.a)->paths == after, true);
    }

    void escapeAbandonsTheStrokeWithoutAnyChange() {
        Fixture f;
        setup(f);
        const auto before = f.window.project_.findObject(f.a)->paths;
        stroke(f.view, QPointF(19.0, 0.0), QPointF(26.0, 0.0), 10, Qt::NoModifier, false);
        QVERIFY(f.window.reshapePreview_ != nullptr);
        QTest::keyClick(f.view, Qt::Key_Escape);
        QVERIFY2(f.window.reshapePreview_ == nullptr, "Échap doit retirer l'aperçu");
        QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, Qt::NoModifier,
                            px(f.view, 26.0, 0.0));
        QCoreApplication::processEvents();
        QCOMPARE(f.window.project_.findObject(f.a)->paths == before, true);
        QVERIFY(!f.window.undoStack_.canUndo());
    }

    void repeatedStrokesDoNotInflateTheNodeCount() {
        Fixture f;
        setup(f);
        for (int i = 0; i < 12; ++i) {
            const double s = (i % 2 == 0) ? 2.0 : -2.0;
            stroke(f.view, QPointF(19.0, 0.0), QPointF(19.0 + s, 0.0), 6);
        }
        const std::size_t n = f.window.project_.findObject(f.a)->paths.front().outer.nodes.size();
        QVERIFY2(n < 72 + 40, qPrintable(QString::number(n)));
    }

    void shiftSmoothsABump() {
        Fixture f;
        setup(f);
        // Crée une bosse puis la lisse avec Maj.
        stroke(f.view, QPointF(19.0, 0.0), QPointF(25.0, 0.0), 10);
        const int bump = maxX(f, f.a);
        stroke(f.view, QPointF(22.0, -4.0), QPointF(22.0, 4.0), 12, Qt::ShiftModifier);
        QVERIFY(maxX(f, f.a) < bump);
    }

    void withoutASelectedShapeNothingHappens() {
        Fixture f;
        setup(f);
        f.window.setSelection({});
        f.window.refreshImage();
        stroke(f.view, QPointF(19.0, 0.0), QPointF(26.0, 0.0), 10);
        QVERIFY(!f.window.undoStack_.canUndo());
    }

    void strokeSuspendsTheStitchLayerUntilItEnds() {
        Fixture f;
        setup(f);
        QVERIFY(!f.window.stitchLayerSuspended_);
        stroke(f.view, QPointF(19.0, 0.0), QPointF(25.0, 0.0), 6, Qt::NoModifier, false);
        QVERIFY(f.window.stitchLayerSuspended_);
        QVERIFY(f.window.stitchItems_.isEmpty());
        f.window.cancelBrushStroke();
        QVERIFY(!f.window.stitchLayerSuspended_);
        stroke(f.view, QPointF(19.0, 0.0), QPointF(25.0, 0.0), 6);
        QTest::qWait(20);
        QVERIFY(!f.window.stitchLayerSuspended_);
    }

    void leavingTheToolRemovesTheRing() {
        Fixture f;
        setup(f);
        QTest::mouseMove(f.view->viewport(), px(f.view, 5.0, 5.0));
        QVERIFY(f.window.brushRing_ != nullptr);
        f.window.setTool(Tool::Select);
        QVERIFY(f.window.brushRing_ == nullptr);
        QVERIFY(!f.window.view_->brushMode());
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_brush_tool.moc"
