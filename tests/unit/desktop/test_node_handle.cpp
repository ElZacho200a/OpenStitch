// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTest>

#include <cmath>
#include <functional>
#include <optional>
#include <utility>
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
    void handleBoundsContainHitTargetWithoutChangingVisual_data();
    void handleBoundsContainHitTargetWithoutChangingVisual();
    void handleHitTargetEdgesSupportQueriesClicksAndDragging_data();
    void handleHitTargetEdgesSupportQueriesClicksAndDragging();
    void overlappingHitTargetsKeepResizeHandleAboveNode_data();
    void overlappingHitTargetsKeepResizeHandleAboveNode();
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

QAbstractGraphicsShapeItem* addHandle(QGraphicsScene& scene, bool resizeHandle,
                                      std::function<void(QPointF)> onReleased,
                                      std::function<void(QPointF)> onMoved = {}) {
    QAbstractGraphicsShapeItem* handle = nullptr;
    if (resizeHandle) {
        handle = new ResizeHandleItem(QPointF(), std::move(onReleased), QPointF(), false,
                                      Qt::SizeFDiagCursor, std::move(onMoved));
    } else {
        handle = new NodeHandleItem(QPointF(), std::move(onReleased), std::move(onMoved));
    }
    scene.addItem(handle);
    return handle;
}

void applyPenMode(QAbstractGraphicsShapeItem& handle, int penMode) {
    if (penMode == 1) {
        handle.setPen(Qt::NoPen);
    } else if (penMode == 2) {
        QPen pen = handle.pen();
        pen.setWidthF(0.0); // stylo cosmétique : autre chemin de boundingRect dans Qt
        handle.setPen(pen);
    }
}
} // namespace

void NodeHandleTest::handleBoundsContainHitTargetWithoutChangingVisual_data() {
    QTest::addColumn<bool>("resizeHandle");
    QTest::addColumn<int>("penMode");
    for (const bool resizeHandle : {false, true}) {
        for (const int penMode : {0, 1, 2}) {
            const QByteArray name =
                QByteArray(resizeHandle ? "resize" : "node") + "-pen" + QByteArray::number(penMode);
            QTest::newRow(name.constData()) << resizeHandle << penMode;
        }
    }
}

void NodeHandleTest::handleBoundsContainHitTargetWithoutChangingVisual() {
    QFETCH(bool, resizeHandle);
    QFETCH(int, penMode);
    QGraphicsScene scene;
    auto* handle = addHandle(scene, resizeHandle, {});
    applyPenMode(*handle, penMode);

    // Le rect peint reste 8 x 8 ; agrandir ce rect masquerait le défaut en agrandissant le visuel.
    const QRectF visual = resizeHandle ? static_cast<ResizeHandleItem*>(handle)->rect()
                                       : static_cast<NodeHandleItem*>(handle)->rect();
    QCOMPARE(visual, QRectF(-4.0, -4.0, 8.0, 8.0));
    QCOMPARE(handle->shape().boundingRect(), QRectF(-11.0, -11.0, 22.0, 22.0));
    QVERIFY(handle->boundingRect().contains(handle->shape().boundingRect()));
    QVERIFY(handle->flags().testFlag(QGraphicsItem::ItemIgnoresTransformations));
    // L'ellipse n'est pas devenue une cible carrée pour satisfaire le préfiltre.
    QCOMPARE(handle->contains(QPointF(10.0, 10.0)), resizeHandle);
}

void NodeHandleTest::handleHitTargetEdgesSupportQueriesClicksAndDragging_data() {
    QTest::addColumn<bool>("resizeHandle");
    QTest::addColumn<int>("penMode");
    QTest::addColumn<double>("zoom");
    QTest::addColumn<QPoint>("offset");
    QTest::addColumn<bool>("indexed");
    for (const bool resizeHandle : {false, true}) {
        for (const int penMode : {0, 1, 2}) {
            for (const double zoom : {0.2, 4.0, 400.0}) {
                for (const QPoint offset :
                     {QPoint(-10, 0), QPoint(10, 0), QPoint(0, -10), QPoint(0, 10)}) {
                    for (const bool indexed : {false, true}) {
                        const QByteArray name =
                            QByteArray(resizeHandle ? "resize" : "node") + "-pen" +
                            QByteArray::number(penMode) + "-zoom" + QByteArray::number(zoom) +
                            "-offset" + QByteArray::number(offset.x()) + "," +
                            QByteArray::number(offset.y()) + (indexed ? "-bsp" : "-noindex");
                        QTest::newRow(name.constData())
                            << resizeHandle << penMode << zoom << offset << indexed;
                    }
                }
            }
        }
    }
}

void NodeHandleTest::handleHitTargetEdgesSupportQueriesClicksAndDragging() {
    QFETCH(bool, resizeHandle);
    QFETCH(int, penMode);
    QFETCH(double, zoom);
    QFETCH(QPoint, offset);
    QFETCH(bool, indexed);
    QGraphicsScene scene;
    scene.setItemIndexMethod(indexed ? QGraphicsScene::BspTreeIndex : QGraphicsScene::NoIndex);
    QGraphicsView view(&scene);
    prepare(view);
    view.setTransform(QTransform::fromScale(zoom, zoom));
    view.centerOn(0.0, 0.0);
    int releasedCount = 0;
    int movedCount = 0;
    std::optional<QPointF> released;
    auto* handle = addHandle(
        scene, resizeHandle,
        [&](QPointF pos) {
            ++releasedCount;
            released = pos;
        },
        [&](QPointF) { ++movedCount; });
    applyPenMode(*handle, penMode);
    // Les coordonnées du viewport sont des DIP, indépendamment du DPR de l'offscreen.
    const QPoint at = view.mapFromScene(handle->pos()) + offset;
    const QTransform deviceTransform = handle->deviceTransform(view.viewportTransform());
    QCOMPARE(deviceTransform.mapRect(handle->shape().boundingRect()).size(), QSizeF(22.0, 22.0));

    // Repro par le vrai chemin Qt, pas seulement par shape().contains().
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QCOMPARE(releasedCount, 1);
    QCOMPARE(handle->pos(), QPointF());
    QCOMPARE(movedCount, 0);

    // Tous les pré-filtres utilisés par la vue, la scène et CanvasView doivent retrouver l'item.
    QCOMPARE(view.itemAt(at), static_cast<QGraphicsItem*>(handle));
    QVERIFY(view.items(at).contains(handle));
    QVERIFY(view.items(QRect(at - QPoint(1, 1), QSize(3, 3))).contains(handle));
    QCOMPARE(scene.itemAt(view.mapToScene(at), view.viewportTransform()),
             static_cast<QGraphicsItem*>(handle));
    QVERIFY(scene
                .items(view.mapToScene(at), Qt::IntersectsItemShape, Qt::DescendingOrder,
                       view.viewportTransform())
                .contains(handle));
    const QPoint outside = view.mapFromScene(handle->pos()) + offset * 2;
    QCOMPARE(view.itemAt(outside), static_cast<QGraphicsItem*>(nullptr));

    const QPoint to = at + QPoint(40, 30);
    sendMouse(view, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(scene.mouseGrabberItem(), static_cast<QGraphicsItem*>(handle));
    sendMouse(view, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    sendMouse(view, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QVERIFY(movedCount > 0);
    QCOMPARE(releasedCount, 2);
    QVERIFY(released.has_value());
    QVERIFY(*released != QPointF());
    QCOMPARE(*released, handle->pos());
}

void NodeHandleTest::overlappingHitTargetsKeepResizeHandleAboveNode_data() {
    QTest::addColumn<double>("zoom");
    for (const double zoom : {0.2, 4.0, 400.0}) {
        QTest::newRow(QByteArray::number(zoom).constData()) << zoom;
    }
}

void NodeHandleTest::overlappingHitTargetsKeepResizeHandleAboveNode() {
    QFETCH(double, zoom);
    QGraphicsScene scene;
    QGraphicsView view(&scene);
    prepare(view);
    view.setTransform(QTransform::fromScale(zoom, zoom));
    view.centerOn(0.0, 0.0);
    int nodeReleased = 0;
    int resizeReleased = 0;
    auto* node = addHandle(scene, false, [&](QPointF) { ++nodeReleased; });
    auto* resize = addHandle(scene, true, [&](QPointF) { ++resizeReleased; });
    const QPoint at = view.mapFromScene(QPointF()) + QPoint(10, 0);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QCOMPARE(resizeReleased, 1);
    QCOMPARE(nodeReleased, 0);
    QCOMPARE(view.itemAt(at), static_cast<QGraphicsItem*>(resize));
    QVERIFY(view.items(at).contains(node));
    QVERIFY(view.items(at).contains(resize));
}

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
