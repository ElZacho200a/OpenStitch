// SPDX-License-Identifier: Apache-2.0
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QTest>

#include <cmath>
#include <optional>
#include <vector>

#include "node_handle.hpp"

using openstitch::desktop::NodeHandleItem;

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
};

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
    auto* handle = new NodeHandleItem(
        QPointF(10.0, -5.0), [&](QPointF p) {
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

QTEST_MAIN(NodeHandleTest)
#include "test_node_handle.moc"
