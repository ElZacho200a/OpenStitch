// SPDX-License-Identifier: Apache-2.0
// Éléments graphiques de la rotation libre (HP-OBJ-004) : pivot déplaçable. Aucune logique
// métier ici : le rappel reçoit la position scène (mm) au relâchement ; l'appelant la convertit
// et la mémorise.
#pragma once

#include <QBrush>
#include <QCursor>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsSceneMouseEvent>
#include <QGuiApplication>
#include <QPainterPath>
#include <QPen>

#include <functional>
#include <utility>

#include "app_theme.hpp"

namespace openstitch::desktop {

// Cadre en tirets de la sélection multiple : purement visuel, sans zone de contact (sinon son
// contour, qui longe les bords des formes, capterait les clics et les survols à leur place).
class SelectionFrameItem : public QGraphicsRectItem {
public:
    using QGraphicsRectItem::QGraphicsRectItem;
    [[nodiscard]] QPainterPath shape() const override { return {}; }
};

// Repère rond du centre de rotation : se glisse ailleurs pour changer le pivot ; un
// double-clic le ramène au centre de la sélection. Taille fixe à l'écran. Tant qu'il est au
// centre de la sélection il laisse passer les clics (sinon il empêcherait de déplacer les
// formes en les saisissant par le milieu) : il ne se saisit alors qu'avec Ctrl+Maj. Une fois
// déplacé, il se saisit normalement.
class PivotHandleItem : public QGraphicsEllipseItem {
public:
    PivotHandleItem(QPointF sceneMm, bool displaced, std::function<void(QPointF)> onReleased,
                    std::function<void()> onReset)
        : QGraphicsEllipseItem(-5.0, -5.0, 10.0, 10.0), displaced_(displaced),
          onReleased_(std::move(onReleased)), onReset_(std::move(onReset)) {
        setPos(sceneMm);
        setFlag(ItemIgnoresTransformations);
        setFlag(ItemIsMovable);
        setBrush(QBrush(AppTheme::instance().tokens().canvasSelectionHalo));
        setPen(QPen(AppTheme::instance().tokens().canvasSelectionLine, 1.5));
        setZValue(102);
        setCursor(Qt::SizeAllCursor);
    }

    // Zone de contact : vide tant que le repère est au centre et que Ctrl+Maj n'est pas enfoncé,
    // pour qu'il ne masque ni la forme dessous (déplacement, survol) ni ses clics.
    [[nodiscard]] QPainterPath shape() const override {
        QPainterPath path;
        if (displaced_ || grabModifiersDown()) {
            path.addEllipse(QRectF(-11.0, -11.0, 22.0, 22.0));
        }
        return path;
    }
    [[nodiscard]] QRectF boundingRect() const override {
        return QGraphicsEllipseItem::boundingRect().united(QRectF(-11.0, -11.0, 22.0, 22.0));
    }

protected:
    static bool grabModifiersDown() {
        const Qt::KeyboardModifiers grab = Qt::ControlModifier | Qt::ShiftModifier;
        return (QGuiApplication::queryKeyboardModifiers() & grab) == grab;
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        if (!displaced_ && !grabModifiersDown()) {
            event->ignore(); // laisse le clic à la forme située dessous
            return;
        }
        QGraphicsEllipseItem::mousePressEvent(event);
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override {
        if (!displaced_) {
            event->ignore();
            return;
        }
        event->accept();
        if (onReset_) {
            onReset_();
        }
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsEllipseItem::mouseReleaseEvent(event);
        if (onReleased_) {
            onReleased_(pos());
        }
    }

private:
    bool displaced_{false};
    std::function<void(QPointF)> onReleased_;
    std::function<void()> onReset_;
};

} // namespace openstitch::desktop
