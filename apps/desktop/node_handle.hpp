// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QApplication>
#include <QBrush>
#include <QCursor>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

#include "app_theme.hpp"

namespace openstitch::desktop {

// Verrou d'axe (lignes M2/N2 de la table des gestes) : ramène `pos` sur l'axe
// dominant du déplacement depuis `start` (horizontal si |dx| >= |dy|). Pur.
[[nodiscard]] inline QPointF axisLockedPos(QPointF start, QPointF pos) {
    const QPointF d = pos - start;
    return std::abs(d.x()) >= std::abs(d.y()) ? QPointF(pos.x(), start.y())
                                              : QPointF(start.x(), pos.y());
}

// Poignée : cercle de taille constante à l'écran, déplaçable. La représentation
// visuelle est petite (8 px) mais la zone d'interaction est plus large (accès
// confortable, cf. accessibilité). Le déplacement est validé au relâchement
// (callback -> commande undo) ; un callback facultatif suit le glisser (aperçu).
class NodeHandleItem : public QGraphicsEllipseItem {
public:
    NodeHandleItem(QPointF sceneMm, std::function<void(QPointF)> onReleased,
                   std::function<void(QPointF)> onMoved = {},
                   std::function<void(QPoint)> onContextMenu = {})
        : QGraphicsEllipseItem(-4.0, -4.0, 8.0, 8.0), onReleased_(std::move(onReleased)),
          onMoved_(std::move(onMoved)), onContextMenu_(std::move(onContextMenu)) {
        setPos(sceneMm);
        setFlag(ItemIgnoresTransformations);
        setFlag(ItemIsMovable);
        setBrush(QBrush(AppTheme::instance().tokens().canvasSelectionHalo));
        setPen(QPen(AppTheme::instance().tokens().canvasNode, 1.5));
        setZValue(100);
        setFlag(ItemIsFocusable); // Échap annule le glisser (cf. keyPressEvent)
        setCursor(Qt::SizeAllCursor);
    }

    // Zone cliquable élargie (22 px, cible confortable à la souris comme au doigt)
    // autour du point visuel.
    [[nodiscard]] QPainterPath shape() const override {
        QPainterPath path;
        path.addEllipse(QRectF(-11.0, -11.0, 22.0, 22.0));
        return path;
    }

    // Variante du rappel de relâchement qui reçoit AUSSI les modificateurs de
    // l'évènement de relâchement (Ctrl = suspendre l'accroche, ligne N2b). Si
    // posé, remplace le rappel simple.
    void setReleasedWithModifiers(std::function<void(QPointF, Qt::KeyboardModifiers)> callback) {
        onReleasedMods_ = std::move(callback);
    }

    // Rappel d'annulation (Échap pendant le glisser) : l'appelant efface ses aperçus.
    void setCancelled(std::function<void()> callback) { onCancelled_ = std::move(callback); }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        pressPos_ = pos();
        pressScreenPos_ = event->screenPos();
        dragStarted_ = false;
        cancelled_ = false;
        setFocus(); // reçoit Échap pendant le glisser
        QGraphicsEllipseItem::mousePressEvent(event);
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
        if (cancelled_) {
            return;
        }
        // Mi9 : tant que la souris n'a pas dépassé le seuil de glisser, le nœud ne bouge
        // pas (un clic légèrement tremblé ne crée ni déplacement ni pas d'annulation).
        if (!dragStarted_) {
            if ((event->screenPos() - pressScreenPos_).manhattanLength() <=
                QApplication::startDragDistance()) {
                return;
            }
            dragStarted_ = true;
        }
        QGraphicsEllipseItem::mouseMoveEvent(event);
        // Maj : verrou d'axe pendant le glisser (ligne N2), lu sur l'évènement.
        if ((event->modifiers() & Qt::ShiftModifier) != 0) {
            setPos(axisLockedPos(pressPos_, pos()));
        }
        if (onMoved_) {
            onMoved_(pos());
        }
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape && !cancelled_) {
            // Annule le glisser : retour à la position d'origine, aucun rappel de
            // relâchement (donc aucune commande).
            cancelled_ = true;
            setPos(pressPos_);
            ungrabMouse();
            if (onCancelled_) {
                onCancelled_();
            }
            event->accept();
            return;
        }
        QGraphicsEllipseItem::keyPressEvent(event);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsEllipseItem::mouseReleaseEvent(event);
        if (cancelled_) {
            return;
        }
        if (!dragStarted_) {
            setPos(pressPos_); // clic sans glisser : position d'origine exacte
        } else if ((event->modifiers() & Qt::ShiftModifier) != 0) {
            setPos(axisLockedPos(pressPos_, pos()));
        }
        if (onReleasedMods_) {
            onReleasedMods_(pos(), event->modifiers());
        } else if (onReleased_) {
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
    std::function<void(QPointF, Qt::KeyboardModifiers)> onReleasedMods_;
    std::function<void(QPointF)> onMoved_;
    std::function<void(QPoint)> onContextMenu_;
    std::function<void()> onCancelled_;
    QPointF pressPos_;
    QPoint pressScreenPos_;
    bool dragStarted_{false};
    bool cancelled_{false};
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

    // Variante du rappel qui reçoit AUSSI les modificateurs de l'évènement de
    // relâchement (Alt = dupliquer en déplaçant, ligne M4). Si posé, remplace le
    // rappel simple.
    void setReleasedWithModifiers(std::function<void(QPointF, Qt::KeyboardModifiers)> callback) {
        onReleasedMods_ = std::move(callback);
    }

protected:
    // Seuil de glisser : un clic dont la souris bouge de quelques pixels ne déplace rien (pas de
    // commande, pas de pas d'annulation) ; au-delà, le déplacement suit la souris sans saut.
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        pressScreenPos_ = event->screenPos();
        dragStarted_ = false;
        QGraphicsPathItem::mousePressEvent(event);
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
        if (!dragStarted_) {
            if ((event->screenPos() - pressScreenPos_).manhattanLength() <=
                QApplication::startDragDistance()) {
                return;
            }
            dragStarted_ = true;
        }
        QGraphicsPathItem::mouseMoveEvent(event);
        // Maj : verrou d'axe pendant le glisser (ligne M2) ; pos() = delta.
        if ((event->modifiers() & Qt::ShiftModifier) != 0) {
            setPos(axisLockedPos(QPointF(0.0, 0.0), pos()));
        }
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsPathItem::mouseReleaseEvent(event);
        if ((event->modifiers() & Qt::ShiftModifier) != 0) {
            setPos(axisLockedPos(QPointF(0.0, 0.0), pos()));
        }
        if (pos() == QPointF(0.0, 0.0)) {
            return;
        }
        if (onReleasedMods_) {
            onReleasedMods_(pos(), event->modifiers());
        } else if (onReleased_) {
            onReleased_(pos());
        }
    }

private:
    std::function<void(QPointF)> onReleased_;
    std::function<void(QPointF, Qt::KeyboardModifiers)> onReleasedMods_;
    QPoint pressScreenPos_;
    bool dragStarted_{false};
};

// Contrainte du glisser d'une poignée de redimensionnement (pure, testable). `anchor` est
// le coin opposé (fixe), `corner` la position d'origine de la poignée, `pos` la position
// souhaitée (coordonnées scène). Les facteurs d'échelle sont planchés à `minScale` : ni
// miroir involontaire (un coin qui franchit l'ancre), ni objet écrasé à zéro. Avec
// `proportional` (Maj), la position est projetée sur la diagonale ancre-coin : même
// facteur sur les deux axes, les proportions de la forme sont conservées.
[[nodiscard]] inline QPointF constrainResizeDrag(QPointF anchor, QPointF corner, QPointF pos,
                                                 bool proportional, double minScale = 0.05) {
    const QPointF d = corner - anchor;
    const QPointF r = pos - anchor;
    double sx = std::abs(d.x()) > 1e-9 ? r.x() / d.x() : 1.0;
    double sy = std::abs(d.y()) > 1e-9 ? r.y() / d.y() : 1.0;
    if (proportional) {
        const double dd = d.x() * d.x() + d.y() * d.y();
        const double t = dd > 1e-12 ? (r.x() * d.x() + r.y() * d.y()) / dd : 1.0;
        sx = t;
        sy = t;
    }
    sx = std::max(sx, minScale);
    sy = std::max(sy, minScale);
    return QPointF(anchor.x() + sx * d.x(), anchor.y() + sy * d.y());
}

// Poignée de redimensionnement : carré (distinct des poignées de nœud,
// rondes) à l'un des 4 coins de la boîte englobante d'une forme SÉLECTIONNÉE.
// Glisser un coin redimensionne la forme entière autour du coin OPPOSÉ (fixe
// par construction — c'est l'appelant qui calcule l'ancrage et les facteurs
// d'échelle à partir de `pos()` au relâchement, cf. ScaleVectorObjectCommand).
// Même principe que NodeHandleItem (taille fixe à l'écran, callback au
// relâchement) mais un carré, pour ne jamais laisser croire qu'on édite un
// nœud du contour. Pendant le glisser, la position est contrainte
// (cf. constrainResizeDrag : planchers, Maj = proportions) et `onMoved` reçoit
// la position contrainte (aperçu et dimensions « L × H mm » côté appelant).
class ResizeHandleItem : public QGraphicsRectItem {
public:
    // `anchorSceneMm` : coin opposé fixe (si `hasAnchor`) ; `cursor` : flèche diagonale
    // propre à ce coin.
    ResizeHandleItem(QPointF sceneMm, std::function<void(QPointF)> onReleased,
                     QPointF anchorSceneMm = {}, bool hasAnchor = false,
                     Qt::CursorShape cursor = Qt::SizeFDiagCursor,
                     std::function<void(QPointF)> onMoved = {})
        : QGraphicsRectItem(-4.0, -4.0, 8.0, 8.0), onReleased_(std::move(onReleased)),
          onMoved_(std::move(onMoved)), anchor_(anchorSceneMm), hasAnchor_(hasAnchor),
          origin_(sceneMm) {
        setPos(sceneMm);
        setFlag(ItemIgnoresTransformations);
        setFlag(ItemIsMovable);
        setFlag(ItemIsFocusable); // Échap annule le glisser
        setBrush(QBrush(AppTheme::instance().tokens().canvasSelectionHalo));
        setPen(QPen(AppTheme::instance().tokens().canvasSelectionLine, 1.5));
        setZValue(101); // au-dessus des poignées de nœud (100) : jamais masquée par elles
        setCursor(cursor);
    }

    // Zone cliquable de 22 px (cible confortable, même taille que les poignées de nœud).
    [[nodiscard]] QPainterPath shape() const override {
        QPainterPath path;
        path.addRect(QRectF(-11.0, -11.0, 22.0, 22.0));
        return path;
    }

    // Rappel d'annulation (Échap pendant le glisser) : l'appelant efface ses aperçus.
    void setCancelled(std::function<void()> callback) { onCancelled_ = std::move(callback); }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        cancelled_ = false;
        setFocus();
        QGraphicsRectItem::mousePressEvent(event);
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
        if (cancelled_) {
            return;
        }
        QGraphicsRectItem::mouseMoveEvent(event);
        constrain(event->modifiers());
        if (onMoved_) {
            onMoved_(pos());
        }
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape && !cancelled_) {
            cancelled_ = true;
            setPos(origin_);
            ungrabMouse();
            if (onCancelled_) {
                onCancelled_();
            }
            event->accept();
            return;
        }
        QGraphicsRectItem::keyPressEvent(event);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsRectItem::mouseReleaseEvent(event);
        if (cancelled_) {
            return;
        }
        constrain(event->modifiers());
        if (onReleased_) {
            onReleased_(pos());
        }
    }

private:
    void constrain(Qt::KeyboardModifiers mods) {
        if (hasAnchor_) {
            setPos(constrainResizeDrag(anchor_, origin_, pos(), (mods & Qt::ShiftModifier) != 0));
        }
    }

    std::function<void(QPointF)> onReleased_;
    std::function<void(QPointF)> onMoved_;
    std::function<void()> onCancelled_;
    QPointF anchor_;
    bool hasAnchor_{false};
    QPointF origin_;
    bool cancelled_{false};
};

} // namespace openstitch::desktop
