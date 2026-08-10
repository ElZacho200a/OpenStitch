// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QBrush>
#include <QCursor>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
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

// Corps d'un objet vectoriel SÉLECTIONNÉ : glisser n'importe où sur la forme
// (pas seulement un nœud) la déplace tout entière — jusqu'ici, seule
// l'édition nœud par nœud existait ; recaler une forme entière exigeait de
// glisser chacun de ses nœuds un par un (défaut remonté en usage réel).
// Contrairement à NodeHandleItem (taille fixe à l'écran), la forme doit
// rester à l'échelle du zoom : pas de ItemIgnoresTransformations. Le tracé
// (`outline`) est en coordonnées scène ABSOLUES (comme construit par
// `objectPainterPath`) ; `pos()` reste donc (0,0) tant qu'aucun glisser n'a
// eu lieu, et devient directement le delta de déplacement au relâchement.
class VectorObjectBodyItem : public QGraphicsPathItem {
public:
    VectorObjectBodyItem(const QPainterPath& outline, QPen pen, QBrush brush,
                         std::function<void(QPointF)> onReleased)
        : QGraphicsPathItem(outline), onReleased_(std::move(onReleased)) {
        setPen(pen);
        setBrush(brush);
        setFlag(ItemIsMovable);
        setCursor(Qt::SizeAllCursor);
    }

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsPathItem::mouseReleaseEvent(event);
        if (onReleased_ && pos() != QPointF(0.0, 0.0)) {
            onReleased_(pos());
        }
    }

private:
    std::function<void(QPointF)> onReleased_;
};

// Poignée de redimensionnement : carré (distinct des poignées de nœud,
// rondes) à l'un des 4 coins de la boîte englobante d'une forme SÉLECTIONNÉE.
// Glisser un coin redimensionne la forme entière autour du coin OPPOSÉ (fixe
// par construction — c'est l'appelant qui calcule l'ancrage et les facteurs
// d'échelle à partir de `pos()` au relâchement, cf. ScaleVectorObjectCommand).
// Même principe que NodeHandleItem (taille fixe à l'écran, callback au
// relâchement) mais un carré, pour ne jamais laisser croire qu'on édite un
// nœud du contour.
class ResizeHandleItem : public QGraphicsRectItem {
public:
    ResizeHandleItem(QPointF sceneMm, std::function<void(QPointF)> onReleased)
        : QGraphicsRectItem(-4.0, -4.0, 8.0, 8.0), onReleased_(std::move(onReleased)) {
        setPos(sceneMm);
        setFlag(ItemIgnoresTransformations);
        setFlag(ItemIsMovable);
        setBrush(QBrush(AppTheme::instance().tokens().canvasSelectionHalo));
        setPen(QPen(AppTheme::instance().tokens().canvasSelectionLine, 1.5));
        setZValue(101);  // au-dessus des poignées de nœud (100) : jamais masquée par elles
        setCursor(Qt::SizeFDiagCursor);
    }

    [[nodiscard]] QPainterPath shape() const override {
        QPainterPath path;
        path.addRect(QRectF(-7.0, -7.0, 14.0, 14.0));
        return path;
    }

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsRectItem::mouseReleaseEvent(event);
        if (onReleased_) {
            onReleased_(pos());
        }
    }

private:
    std::function<void(QPointF)> onReleased_;
};

}  // namespace openstitch::desktop
