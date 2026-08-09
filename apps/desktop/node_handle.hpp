// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QBrush>
#include <QCursor>
#include <QGraphicsEllipseItem>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QPen>

#include <functional>
#include <utility>

#include "app_theme.hpp"

namespace openstitch::desktop {

// Poignée : cercle de taille constante à l'écran, déplaçable. La représentation
// visuelle est petite (8 px) mais la zone d'interaction est plus large (accès
// confortable, cf. accessibilité). Le déplacement est validé au relâchement
// (callback -> commande undo) ; un callback facultatif suit le glisser (aperçu).
class NodeHandleItem : public QGraphicsEllipseItem {
public:
    NodeHandleItem(QPointF sceneMm, std::function<void(QPointF)> onReleased,
                   std::function<void(QPointF)> onMoved = {},
                   std::function<void(QPoint)> onContextMenu = {})
        : QGraphicsEllipseItem(-4.0, -4.0, 8.0, 8.0),
          onReleased_(std::move(onReleased)),
          onMoved_(std::move(onMoved)),
          onContextMenu_(std::move(onContextMenu)) {
        setPos(sceneMm);
        setFlag(ItemIgnoresTransformations);
        setFlag(ItemIsMovable);
        setBrush(QBrush(AppTheme::instance().tokens().canvasSelectionHalo));
        setPen(QPen(AppTheme::instance().tokens().canvasNode, 1.5));
        setZValue(100);
        setCursor(Qt::SizeAllCursor);
    }

    // Zone cliquable élargie (14 px) autour du point visuel.
    [[nodiscard]] QPainterPath shape() const override {
        QPainterPath path;
        path.addEllipse(QRectF(-7.0, -7.0, 14.0, 14.0));
        return path;
    }

protected:
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsEllipseItem::mouseMoveEvent(event);
        if (onMoved_) {
            onMoved_(pos());
        }
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsEllipseItem::mouseReleaseEvent(event);
        if (onReleased_) {
            onReleased_(pos());
        }
    }
    // Clic droit : menu contextuel (ex. « Supprimer le nœud »), optionnel —
    // sans callback fourni, comportement par défaut de QGraphicsItem (rien).
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override {
        if (onContextMenu_) {
            onContextMenu_(event->screenPos());
            event->accept();
        } else {
            QGraphicsEllipseItem::contextMenuEvent(event);
        }
    }

private:
    std::function<void(QPointF)> onReleased_;
    std::function<void(QPointF)> onMoved_;
    std::function<void(QPoint)> onContextMenu_;
};

}  // namespace openstitch::desktop
