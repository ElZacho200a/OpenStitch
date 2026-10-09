// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTest>

#include <cmath>
#include <optional>
#include <vector>

#include "node_handle.hpp"

using openstitch::desktop::axisLockedPos;
using openstitch::desktop::constrainResizeDrag;
using openstitch::desktop::NodeHandleItem;
using openstitch::desktop::ResizeHandleItem;
using openstitch::desktop::VectorObjectBodyItem;

// NodeHandleItem est le mécanisme Qt réellement utilisé pour déplacer un
// nœud (cf. apps/desktop/main_window.cpp : le glisser d'une poignée déclenche
// commands::MoveNodeCommand). MoveNodeCommand lui-même est déjà testé côté
// coeur (tests/unit/commands/test_undo_stack.cpp, cas "AddVectorObject et
// MoveNode"). Ce test couvre la partie encore non testée : la conversion
// glisser-déposer Qt -> callback avec la bonne position scène.
class NodeHandleTest : public QObject {
    Q_OBJECT

private slots:
    void draggingHandleInvokesMovedThenReleasedWithSceneCoordinates();
    void unmovedClickStillInvokesReleasedAtSamePosition();
    void axisLockKeepsTheDominantAxis();
    void shiftLocksTheHandleAxisWhileDragging();
    void releaseCallbackWithModifiersReceivesTheReleaseModifiers();
    void bodyItemShiftLocksAxisAndReportsModifiers();
    void jitterBelowTheDragThresholdDoesNotMoveTheHandle();
    void escapeCancelsTheDragWithoutRelease();
    void resizeConstraintFloorsScalesAndNeverMirrors();
    void resizeConstraintWithShiftKeepsProportions();
    void resizeHandleReportsConstrainedPositionAndHasWideHitArea();
};

namespace {
void sendMouse(QGraphicsView& view, QEvent::Type type, QPoint at, Qt::MouseButton button,
               Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
    QMouseEvent event(type, QPointF(at), QPointF(view.viewport()->mapToGlobal(at)), button, buttons,
                      mods);
    QApplication::sendEvent(view.viewport(), &event);
}

void prepare(QGraphicsView& view) {
    view.setSceneRect(-150.0, -150.0, 300.0, 300.0);
    view.resize(300, 300);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.centerOn(0.0, 0.0);
}
} // namespace

void NodeHandleTest::draggingHandleInvokesMovedThenReleasedWithSceneCoordinates() {
    QGraphicsScene scene;
    QGraphicsView view(&scene);
    view.setSceneRect(-150.0, -150.0, 300.0, 300.0);
    view.resize(300, 300);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.centerOn(0.0, 0.0);

    std::vector<QPointF> movedPositions;
    std::optional<QPointF> releasedPosition;
    auto* handle = new NodeHandleItem(
        QPointF(0.0, 0.0), [&](QPointF p) { releasedPosition = p; },
        [&](QPointF p) { movedPositions.push_back(p); });
    scene.addItem(handle);

    const QPoint startVp = view.mapFromScene(handle->pos());
    const QPoint midVp = startVp + QPoint(30, 20);
    const QPoint endVp = startVp + QPoint(60, 40);

    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, startVp);
    QTest::mouseMove(view.viewport(), midVp);
    QTest::mouseMove(view.viewport(), endVp);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, endVp);

    QVERIFY(!movedPositions.empty());
    QVERIFY(releasedPosition.has_value());

    const QPointF expectedScene = view.mapToScene(endVp);
    QVERIFY(std::abs(releasedPosition->x() - expectedScene.x()) < 1.0);
    QVERIFY(std::abs(releasedPosition->y() - expectedScene.y()) < 1.0);
    // Le callback de relâchement reçoit exactement la position finale de l'item.
    QCOMPARE(handle->pos(), *releasedPosition);
    // L'item a réellement bougé (pas un no-op).
    QVERIFY(std::abs(handle->pos().x()) > 0.1 || std::abs(handle->pos().y()) > 0.1);
}

void NodeHandleTest::unmovedClickStillInvokesReleasedAtSamePosition() {
    QGraphicsScene scene;
    QGraphicsView view(&scene);
    view.setSceneRect(-150.0, -150.0, 300.0, 300.0);
    view.resize(300, 300);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.centerOn(0.0, 0.0);

    int releasedCount = 0;
    std::optional<QPointF> releasedPosition;
    auto* handle = new NodeHandleItem(QPointF(10.0, -5.0), [&](QPointF p) {
        ++releasedCount;
        releasedPosition = p;
    });
    scene.addItem(handle);

    const QPoint clickVp = view.mapFromScene(handle->pos());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, clickVp);

    QCOMPARE(releasedCount, 1);
    QVERIFY(releasedPosition.has_value());
    QVERIFY(std::abs(releasedPosition->x() - 10.0) < 0.5);
    QVERIFY(std::abs(releasedPosition->y() - (-5.0)) < 0.5);
}

void NodeHandleTest::axisLockKeepsTheDominantAxis() {
    QCOMPARE(axisLockedPos(QPointF(1.0, 1.0), QPointF(5.0, 2.0)), QPointF(5.0, 1.0));
    QCOMPARE(axisLockedPos(QPointF(1.0, 1.0), QPointF(2.0, -6.0)), QPointF(1.0, -6.0));
    // Égalité : horizontal. Aucun déplacement : inchangé.
    QCOMPARE(axisLockedPos(QPointF(0.0, 0.0), QPointF(3.0, 3.0)), QPointF(3.0, 0.0));
    QCOMPARE(axisLockedPos(QPointF(2.0, 2.0), QPointF(2.0, 2.0)), QPointF(2.0, 2.0));
}

void NodeHandleTest::shiftLocksTheHandleAxisWhileDragging() {
    QGraphicsScene scene;
    QGraphicsView view(&scene);
    prepare(view);
    std::vector<QPointF> moved;
    std::optional<QPointF> released;
    auto* handle = new NodeHandleItem(
        QPointF(0.0, 0.0), [&](QPointF p) { released = p; },
        [&](QPointF p) { moved.push_back(p); });
    scene.addItem(handle);
    const QPoint start = view.mapFromScene(handle->pos());

    sendMouse(view, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton,
              Qt::NoModifier);
    sendMouse(view, QEvent::MouseMove, start + QPoint(40, 10), Qt::NoButton, Qt::LeftButton,
              Qt::ShiftModifier);
    QVERIFY(!moved.empty());
    QCOMPARE(moved.back().y(), 0.0); // verrouillé sur x
    QVERIFY(moved.back().x() > 1.0);
    // Sans Maj, le glisser reste libre (le verrou n'est pas collant).
    sendMouse(view, QEvent::MouseMove, start + QPoint(40, 30), Qt::NoButton, Qt::LeftButton,
              Qt::NoModifier);
    QVERIFY(moved.back().y() > 1.0);
    // Maj tenu seulement au relâchement : le verrou s'applique à la position finale.
    sendMouse(view, QEvent::MouseButtonRelease, start + QPoint(40, 30), Qt::LeftButton,
              Qt::NoButton, Qt::ShiftModifier);
    QVERIFY(released.has_value());
    QCOMPARE(released->y(), 0.0);
    QVERIFY(released->x() > 1.0);
}

void NodeHandleTest::releaseCallbackWithModifiersReceivesTheReleaseModifiers() {
    QGraphicsScene scene;
    QGraphicsView view(&scene);
    prepare(view);
    int simple = 0;
    std::optional<Qt::KeyboardModifiers> mods;
    auto* handle = new NodeHandleItem(QPointF(0.0, 0.0), [&](QPointF) { ++simple; });
    handle->setReleasedWithModifiers([&](QPointF, Qt::KeyboardModifiers m) { mods = m; });
    scene.addItem(handle);
    const QPoint start = view.mapFromScene(handle->pos());
    sendMouse(view, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton,
              Qt::NoModifier);
    sendMouse(view, QEvent::MouseMove, start + QPoint(20, 20), Qt::NoButton, Qt::LeftButton,
              Qt::ControlModifier);
    sendMouse(view, QEvent::MouseButtonRelease, start + QPoint(20, 20), Qt::LeftButton,
              Qt::NoButton, Qt::ControlModifier);
    QVERIFY(mods.has_value());
    QVERIFY((*mods & Qt::ControlModifier) != 0);
    QCOMPARE(simple, 0); // la variante avec modificateurs remplace le rappel simple
}

void NodeHandleTest::bodyItemShiftLocksAxisAndReportsModifiers() {
    QGraphicsScene scene;
    QGraphicsView view(&scene);
    prepare(view);
    QPainterPath outline;
    outline.addRect(-20.0, -20.0, 40.0, 40.0);
    std::optional<QPointF> delta;
    std::optional<Qt::KeyboardModifiers> mods;
    auto* body = new VectorObjectBodyItem(outline, QPen(), QBrush(Qt::gray), {});
    body->setReleasedWithModifiers([&](QPointF d, Qt::KeyboardModifiers m) {
        delta = d;
        mods = m;
    });
    scene.addItem(body);
    const QPoint start = view.mapFromScene(QPointF(0.0, 0.0));
    sendMouse(view, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton,
              Qt::NoModifier);
    sendMouse(view, QEvent::MouseMove, start + QPoint(10, 40), Qt::NoButton, Qt::LeftButton,
              Qt::ShiftModifier | Qt::AltModifier);
    sendMouse(view, QEvent::MouseButtonRelease, start + QPoint(10, 40), Qt::LeftButton,
              Qt::NoButton, Qt::ShiftModifier | Qt::AltModifier);
    QVERIFY(delta.has_value());
    QCOMPARE(delta->x(), 0.0);
    QVERIFY(delta->y() > 1.0);
    QVERIFY((*mods & Qt::AltModifier) != 0);
    QVERIFY((*mods & Qt::ShiftModifier) != 0);
}

void NodeHandleTest::jitterBelowTheDragThresholdDoesNotMoveTheHandle() {
    QGraphicsScene scene;
    QGraphicsView view(&scene);
    prepare(view);
    int movedCount = 0;
    std::optional<QPointF> released;
    auto* handle = new NodeHandleItem(
        QPointF(3.0, 4.0), [&](QPointF p) { released = p; }, [&](QPointF) { ++movedCount; });
    scene.addItem(handle);
    const QPoint start = view.mapFromScene(handle->pos());
    sendMouse(view, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton,
              Qt::NoModifier);
    sendMouse(view, QEvent::MouseMove, start + QPoint(2, 1), Qt::NoButton, Qt::LeftButton,
              Qt::NoModifier);
    sendMouse(view, QEvent::MouseButtonRelease, start + QPoint(2, 1), Qt::LeftButton, Qt::NoButton,
              Qt::NoModifier);
    QCOMPARE(movedCount, 0);
    QVERIFY(released.has_value());
    QCOMPARE(*released, QPointF(3.0, 4.0)); // position d'origine exacte : aucun déplacement
}

void NodeHandleTest::escapeCancelsTheDragWithoutRelease() {
    QGraphicsScene scene;
    QGraphicsView view(&scene);
    prepare(view);
    int releaseCount = 0;
    int cancelCount = 0;
    auto* handle = new NodeHandleItem(QPointF(0.0, 0.0), [&](QPointF) { ++releaseCount; });
    handle->setCancelled([&] { ++cancelCount; });
    scene.addItem(handle);
    const QPoint start = view.mapFromScene(handle->pos());
    sendMouse(view, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton,
              Qt::NoModifier);
    sendMouse(view, QEvent::MouseMove, start + QPoint(50, 30), Qt::NoButton, Qt::LeftButton,
              Qt::NoModifier);
    QVERIFY(handle->pos() != QPointF(0.0, 0.0));
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&scene, &esc);
    QCOMPARE(handle->pos(), QPointF(0.0, 0.0));
    QCOMPARE(cancelCount, 1);
    sendMouse(view, QEvent::MouseButtonRelease, start + QPoint(50, 30), Qt::LeftButton,
              Qt::NoButton, Qt::NoModifier);
    QCOMPARE(releaseCount, 0); // aucune commande créée
}

void NodeHandleTest::resizeConstraintFloorsScalesAndNeverMirrors() {
    const QPointF anchor(0.0, 0.0);
    const QPointF corner(10.0, 20.0);
    // Glisser au-delà de l'ancre (miroir) : plancher à 5 % de la taille d'origine.
    const QPointF crossed = constrainResizeDrag(anchor, corner, QPointF(-30.0, -5.0), false);
    QCOMPARE(crossed, QPointF(0.5, 1.0));
    // Un seul axe sous le plancher : l'autre reste libre.
    const QPointF oneAxis = constrainResizeDrag(anchor, corner, QPointF(20.0, -3.0), false);
    QCOMPARE(oneAxis, QPointF(20.0, 1.0));
    // Redimensionnement normal inchangé.
    QCOMPARE(constrainResizeDrag(anchor, corner, QPointF(5.0, 40.0), false), QPointF(5.0, 40.0));
    // Coin dont les coordonnées sont négatives par rapport à l'ancre : même plancher.
    const QPointF c2(-10.0, -20.0);
    QCOMPARE(constrainResizeDrag(anchor, c2, QPointF(8.0, 8.0), false), QPointF(-0.5, -1.0));
}

void NodeHandleTest::resizeConstraintWithShiftKeepsProportions() {
    const QPointF anchor(0.0, 0.0);
    const QPointF corner(10.0, 20.0);
    const QPointF out = constrainResizeDrag(anchor, corner, QPointF(30.0, 20.0), true);
    // Même facteur sur les deux axes : rapport 1:2 conservé.
    QVERIFY(std::abs(out.y() - 2.0 * out.x()) < 1e-9);
    QVERIFY(out.x() > 10.0);
    // Plancher aussi en proportionnel.
    const QPointF tiny = constrainResizeDrag(anchor, corner, QPointF(-50.0, -50.0), true);
    QCOMPARE(tiny, QPointF(0.5, 1.0));
}

void NodeHandleTest::resizeHandleReportsConstrainedPositionAndHasWideHitArea() {
    QGraphicsScene scene;
    QGraphicsView view(&scene);
    prepare(view);
    std::vector<QPointF> moved;
    std::optional<QPointF> released;
    auto* handle = new ResizeHandleItem(
        QPointF(40.0, 40.0), [&](QPointF p) { released = p; }, QPointF(0.0, 0.0), true,
        Qt::SizeBDiagCursor, [&](QPointF p) { moved.push_back(p); });
    scene.addItem(handle);
    QVERIFY(handle->shape().boundingRect().width() >= 22.0);
    QCOMPARE(handle->cursor().shape(), Qt::SizeBDiagCursor);
    const QPoint start = view.mapFromScene(handle->pos());
    sendMouse(view, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton,
              Qt::NoModifier);
    // Traverse l'ancre : le résultat reste du bon côté (aucun miroir).
    sendMouse(view, QEvent::MouseMove, start + QPoint(-120, -120), Qt::NoButton, Qt::LeftButton,
              Qt::NoModifier);
    sendMouse(view, QEvent::MouseButtonRelease, start + QPoint(-120, -120), Qt::LeftButton,
              Qt::NoButton, Qt::NoModifier);
    QVERIFY(!moved.empty());
    QVERIFY(released.has_value());
    QVERIFY(released->x() > 0.0);
    QVERIFY(released->y() > 0.0);
}

QTEST_MAIN(NodeHandleTest)
#include "test_node_handle.moc"
