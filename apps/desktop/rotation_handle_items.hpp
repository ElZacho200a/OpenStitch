// SPDX-License-Identifier: Apache-2.0
// Éléments graphiques de la rotation libre (HP-OBJ-004) : pivot déplaçable. Aucune logique
// métier ici : le rappel reçoit la position scène (mm) au relâchement ; l'appelant la convertit
// et la mémorise.
#pragma once

#include <QBrush>
#include <QCursor>
#include <QGraphicsEllipseItem>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QPen>

#include <functional>
#include <utility>

#include "app_theme.hpp"

namespace openstitch::desktop {

// Repère rond du centre de rotation : se glisse ailleurs pour changer le pivot ; un
// double-clic le ramène au centre de la sélection. Taille fixe à l'écran.
class PivotHandleItem : public QGraphicsEllipseItem {
public:
    PivotHandleItem(QPointF sceneMm, std::function<void(QPointF)> onReleased,
                    std::function<void()> onReset)
        : QGraphicsEllipseItem(-5.0, -5.0, 10.0, 10.0), onReleased_(std::move(onReleased)),
          onReset_(std::move(onReset)) {
        setPos(sceneMm);
        setFlag(ItemIgnoresTransformations);
        setFlag(ItemIsMovable);
        setBrush(QBrush(AppTheme::instance().tokens().canvasSelectionHalo));
        setPen(QPen(AppTheme::instance().tokens().canvasSelectionLine, 1.5));
        setZValue(102);
        setCursor(Qt::SizeAllCursor);
    }

    [[nodiscard]] QPainterPath shape() const override {
        QPainterPath path;
        path.addEllipse(QRectF(-11.0, -11.0, 22.0, 22.0));
        return path;
    }
    [[nodiscard]] QRectF boundingRect() const override {
        return QGraphicsEllipseItem::boundingRect().united(QRectF(-11.0, -11.0, 22.0, 22.0));
    }

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsEllipseItem::mouseReleaseEvent(event);
        if (onReleased_) {
            onReleased_(pos());
        }
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override {
        event->accept();
        if (onReset_) {
            onReset_();
        }
    }

private:
    std::function<void(QPointF)> onReleased_;
    std::function<void()> onReset_;
};

} // namespace openstitch::desktop
