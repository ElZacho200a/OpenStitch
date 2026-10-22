// SPDX-License-Identifier: Apache-2.0
// Remodelage direct d'un segment (HP-VEC-016) avec de VRAIS gestes souris sur le canevas :
// glisser sur le contour tire le segment, glisser à l'intérieur déplace toujours la forme,
// Échap annule sans toucher au document. Headless, sans pixel ni sleep.
#include <QGraphicsPathItem>
#include <QKeyEvent>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/document/project.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

// Carré de 30 mm de côté centré sur l'origine.
geo::Path square30() {
    geo::Path path;
    path.closed = true;
    for (const auto& [x, y] :
         {std::pair{-15'000, -15'000}, {15'000, -15'000}, {15'000, 15'000}, {-15'000, 15'000}}) {
        path.nodes.push_back({Vec2um{Micrometers{x}, Micrometers{y}}, geo::NodeType::Corner,
                              std::nullopt, std::nullopt});
    }
    return path;
}

doc::Project twoSquares(ObjectId& first, ObjectId& second) {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    for (const int dx : {0, 80'000}) {
        doc::VectorObject object;
        object.id = project.object_ids.next();
        object.name = "Carre";
        geo::Path p = square30();
        for (auto& n : p.nodes) {
            n.pos.x = Micrometers{n.pos.x.value + dx};
        }
        object.paths.push_back({p, {}});
        (dx == 0 ? first : second) = object.id;
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
        f.window.applyLoadedProject(twoSquares(f.a, f.b));
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

    static QPoint px(CanvasView* view, double xMm, double yMmSceneDown) {
        return view->mapFromScene(QPointF(xMm, yMmSceneDown));
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("SegmentReshapeTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void draggingTheOutlinePullsTheSegmentInOneUndoStep() {
        Fixture f;
        setup(f);
        const auto before = f.window.project_.findObject(f.a)->paths;
        const auto otherBefore = f.window.project_.findObject(f.b)->paths;
        // Bord bas du carré : y modèle = -15 mm -> scène +15 mm.
        const QPoint from = px(f.view, 0.0, 15.0);
        const QPoint to = px(f.view, 0.0, 22.0);
        QTest::mousePress(f.view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(f.view->viewport(), (from + to) / 2);
        QTest::mouseMove(f.view->viewport(), to);
        QVERIFY2(f.window.reshapePreview_ != nullptr, "aperçu du remodelage attendu");
        // Aucune commande tant que la souris est enfoncée.
        QVERIFY(!f.window.undoStack_.canUndo());
        QCOMPARE(f.window.project_.findObject(f.a)->paths == before, true);
        QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCoreApplication::processEvents();

        const auto& after = f.window.project_.findObject(f.a)->paths;
        QVERIFY(after != before);
        const auto& nodes = after.front().outer.nodes;
        QCOMPARE(nodes.size(), size_t{4});
        for (std::size_t i = 0; i < 4; ++i) {
            QCOMPARE(nodes[i].pos, before.front().outer.nodes[i].pos); // extrémités intactes
        }
        QVERIFY(nodes[0].tan_out.has_value()); // le bord bas est devenu une courbe
        QVERIFY(nodes[1].tan_in.has_value());
        // L'autre objet n'est pas touché ; l'aperçu est nettoyé.
        QCOMPARE(f.window.project_.findObject(f.b)->paths == otherBefore, true);
        QVERIFY(f.window.reshapePreview_ == nullptr);

        const auto afterCopy = after;
        QVERIFY(f.window.undoStack_.undo(f.window.project_));
        QCOMPARE(f.window.project_.findObject(f.a)->paths == before, true);
        QVERIFY(!f.window.undoStack_.canUndo()); // un seul pas d'annulation
        QVERIFY(f.window.undoStack_.redo(f.window.project_));
        QCOMPARE(f.window.project_.findObject(f.a)->paths == afterCopy, true);
    }

    void draggingInsideStillMovesTheWholeShape() {
        Fixture f;
        setup(f);
        const auto before = f.window.project_.findObject(f.a)->paths;
        const QPoint from = px(f.view, 0.0, 0.0);
        const QPoint to = px(f.view, 5.0, 0.0);
        QTest::mousePress(f.view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(f.view->viewport(), (from + to) / 2);
        QTest::mouseMove(f.view->viewport(), to);
        QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCoreApplication::processEvents();
        const auto& nodes = f.window.project_.findObject(f.a)->paths.front().outer.nodes;
        QVERIFY(nodes[0].pos != before.front().outer.nodes[0].pos); // la forme a bougé
        QVERIFY(!nodes[0].tan_out.has_value());                     // mais n'a pas été courbée
        QCOMPARE(nodes[1].pos.x.value - nodes[0].pos.x.value, 30'000); // même forme
    }

    void escapeCancelsTheReshapeWithoutAnyChange() {
        Fixture f;
        setup(f);
        const auto before = f.window.project_.findObject(f.a)->paths;
        const QPoint from = px(f.view, 0.0, 15.0);
        const QPoint to = px(f.view, 0.0, 22.0);
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

    void aSimpleClickOnTheOutlineChangesNothing() {
        Fixture f;
        setup(f);
        const auto before = f.window.project_.findObject(f.a)->paths;
        const QPoint at = px(f.view, 0.0, 15.0);
        QTest::mouseClick(f.view->viewport(), Qt::LeftButton, Qt::NoModifier, at);
        QCoreApplication::processEvents();
        QCOMPARE(f.window.project_.findObject(f.a)->paths == before, true);
        QVERIFY(!f.window.undoStack_.canUndo());
        QVERIFY(f.window.reshapePreview_ == nullptr);
    }

    void hoverDistinguishesSegmentReshapeFromMovingTheWholeShape() {
        Fixture f;
        setup(f);
        const QPoint inside = px(f.view, 0.0, 0.0);
        const QPoint outline = px(f.view, 0.0, 15.0);
        auto* body = dynamic_cast<VectorObjectBodyItem*>(f.view->itemAt(inside));
        QVERIFY(body != nullptr);
        QTest::mouseMove(f.view->viewport(), inside);
        QCOMPARE(body->cursor().shape(), Qt::SizeAllCursor);
        QTest::mouseMove(f.view->viewport(), outline);
        QCOMPARE(body->cursor().shape(), Qt::CrossCursor);
        QTest::mouseMove(f.view->viewport(), inside);
        QCOMPARE(body->cursor().shape(), Qt::SizeAllCursor);
        QVERIFY(!f.window.undoStack_.canUndo()); // le survol ne change pas le projet
    }

    void aPullThatWouldCrossTheContourIsRefused() {
        Fixture f;
        setup(f);
        const auto before = f.window.project_.findObject(f.a)->paths;
        const QPoint from = px(f.view, 0.0, 15.0);
        const QPoint to = px(f.view, 0.0, -60.0); // à travers toute la forme
        QTest::mousePress(f.view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(f.view->viewport(), (from + to) / 2);
        QTest::mouseMove(f.view->viewport(), to);
        QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCoreApplication::processEvents();
        QCOMPARE(f.window.project_.findObject(f.a)->paths == before, true);
        QVERIFY(!f.window.undoStack_.canUndo());
        QVERIFY(f.window.statusBar()->currentMessage().contains(QStringLiteral("croiser")));
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_segment_reshape.moc"
