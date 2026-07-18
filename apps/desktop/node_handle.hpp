// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QBrush>
#include <QCursor>
#include <QGraphicsEllipseItem>
#include <QGraphicsSceneMouseEvent>
#include <QPen>

#include <functional>
#include <utility>

namespace openstitch::desktop {

// Poignée de nœud : cercle de taille constante à l'écran, déplaçable.
// Le déplacement est validé au relâchement (callback -> commande undo).
class NodeHandleItem : public QGraphicsEllipseItem {
public:
    NodeHandleItem(QPointF sceneMm, std::function<void(QPointF)> onReleased)
        : QGraphicsEllipseItem(-4.0, -4.0, 8.0, 8.0), onReleased_(std::move(onReleased)) {
        setPos(sceneMm);
        setFlag(ItemIgnoresTransformations);
        setFlag(ItemIsMovable);
        setBrush(QBrush(QColor(255, 255, 255)));
        setPen(QPen(QColor(30, 90, 200), 1.5));
        setZValue(100);
        setCursor(Qt::SizeAllCursor);
    }

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsEllipseItem::mouseReleaseEvent(event);
        if (onReleased_) {
            onReleased_(pos());
        }
    }

private:
    std::function<void(QPointF)> onReleased_;
};

}  // namespace openstitch::desktop
