// SPDX-License-Identifier: Apache-2.0
#include "canvas_view.hpp"

#include <QContextMenuEvent>
#include <QGraphicsItem>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

#include "app_theme.hpp"

namespace openstitch::desktop {

namespace {
constexpr double kZoomStep = 1.15;
constexpr double kMinPxPerMm = 0.2;    // motif de 1 m visible en entier
constexpr double kMaxPxPerMm = 400.0;  // 0,1 mm = 40 px : largement assez fin

// Plus petit pas « rond » (en mm) dont la taille à l'écran atteint minPixels.
double niceStepMm(double pxPerMm, double minPixels) {
    static constexpr double steps[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500};
    for (const double s : steps) {
        if (s * pxPerMm >= minPixels) {
            return s;
        }
    }
    return 1000.0;
}
}  // namespace

CanvasView::CanvasView(QGraphicsScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setFrameShape(QFrame::NoFrame);  // le viewport s'aligne avec les règles
    setMouseTracking(true);
    // Rendu de gros motifs : ne pas sauvegarder l'état du peintre entre items,
    // et ne repeindre que la zone modifiée plutôt que tout le viewport.
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setCacheMode(QGraphicsView::CacheBackground);
    setBackgroundBrush(AppTheme::instance().tokens().canvasBackground);
    // Rejoue le fond et le contenu quand le thème change.
    connect(&AppTheme::instance(), &AppTheme::changed, this, [this] {
        setBackgroundBrush(AppTheme::instance().tokens().canvasBackground);
        resetCachedContent();
        viewport()->update();
    });

    // Note : fromScenePoint/toScenePoint (paramètres du signal) accusent un
    // retard d'une étape sur viewportRect — ils reflètent la position du
    // glisser précédent, pas la courante (constaté par test, invisible en
    // usage réel où les mouvements sont pixel à pixel, mais faux pour un
    // glisser rapide à peu d'évènements). On reconvertit donc viewportRect
    // lui-même, toujours à jour.
    connect(this, &QGraphicsView::rubberBandChanged, this, [this](QRect viewportRect) {
        if (!viewportRect.isNull()) {
            lastRubberBandMm_ = mapToScene(viewportRect).boundingRect();
        }
    });
}

void CanvasView::applyModeCursor(Qt::CursorShape shape) {
    setCursor(shape);
    if (viewport() != nullptr) {
        viewport()->setCursor(shape);
    }
}

// Recalcule dragMode() à partir de TOUS les booléens de mode courants (pas
// seulement celui qu'on vient de changer) : défaut trouvé par revue (retour
// utilisateur : rectangle/ellipse "ne fonctionnent pas au clic-glisser").
// MainWindow::setTool() appelle les six méthodes set*Mode ci-dessous à la
// suite, une seule à `true` (l'outil actif) et les cinq autres à `false` --
// quand chacune fixait dragMode() en ne regardant QUE son propre booléen
// (ex. `setDragMode(enabled ? RubberBandDrag : ScrollHandDrag)`), la
// dernière méthode appelée écrasait systématiquement le mode posé par les
// précédentes, quel que soit l'outil réellement actif (setBoxDrawMode(true)
// posait RubberBandDrag, puis setPolygonDrawMode(false) l'écrasait aussitôt
// en ScrollHandDrag). Aucun test ne l'a repéré car tous injectent
// directement le signal Qt final (boxDrawnMm, etc.), jamais la mécanique
// souris réelle bout en bout. En recalculant depuis l'état COMPLET à chaque
// appel, l'ordre d'appel n'a plus d'importance et chaque méthode reste
// correcte utilisée seule (comme le font les tests de CanvasView isolé).
void CanvasView::updateDragMode() {
    if (cropMode_ || boxDrawMode_) {
        setDragMode(QGraphicsView::RubberBandDrag);
    } else if (polygonDrawMode_ || freeformDrawMode_ || satinPairDrawMode_ || bezierDrawMode_) {
        setDragMode(QGraphicsView::NoDrag);
    } else {
        setDragMode(QGraphicsView::ScrollHandDrag);
    }
}

void CanvasView::setCropMode(bool enabled) {
    cropMode_ = enabled;
    lastRubberBandMm_ = QRectF();
    updateDragMode();
    applyModeCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasView::setBoxDrawMode(bool enabled) {
    boxDrawMode_ = enabled;
    lastRubberBandMm_ = QRectF();
    updateDragMode();
    applyModeCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasView::setPolygonDrawMode(bool enabled) {
    polygonDrawMode_ = enabled;
    updateDragMode();
    applyModeCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasView::setFreeformDrawMode(bool enabled) {
    freeformDrawMode_ = enabled;
    freeformActive_ = false;
    updateDragMode();
    applyModeCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasView::setSatinPairDrawMode(bool enabled) {
    satinPairDrawMode_ = enabled;
    updateDragMode();
    applyModeCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasView::setBezierDrawMode(bool enabled) {
    bezierDrawMode_ = enabled;
    bezierPressActive_ = false;
    updateDragMode();
    applyModeCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasView::setCanvasSizeMm(QSizeF sizeMm) {
    canvasMm_ = sizeMm;
    // Marge de travail autour du canevas : la moitié de sa taille de chaque côté.
    const QRectF canvas(-sizeMm.width() / 2.0, -sizeMm.height() / 2.0, sizeMm.width(),
                        sizeMm.height());
    scene()->setSceneRect(canvas.adjusted(-sizeMm.width() / 2.0, -sizeMm.height() / 2.0,
                                          sizeMm.width() / 2.0, sizeMm.height() / 2.0));
    viewport()->update();
    emit viewChanged();
}

double CanvasView::pixelsPerMm() const {
    return transform().m11();
}

void CanvasView::zoomIn() {
    applyZoom(kZoomStep, false);
}

void CanvasView::zoomOut() {
    applyZoom(1.0 / kZoomStep, false);
}

void CanvasView::fitCanvas() {
    const QRectF canvas(-canvasMm_.width() / 2.0, -canvasMm_.height() / 2.0, canvasMm_.width(),
                        canvasMm_.height());
    fitInView(canvas.adjusted(-5, -5, 5, 5), Qt::KeepAspectRatio);
    emit viewChanged();
}

void CanvasView::applyZoom(double factor, bool anchorUnderMouse) {
    const double current = pixelsPerMm();
    const double target = std::clamp(current * factor, kMinPxPerMm, kMaxPxPerMm);
    factor = target / current;
    if (factor == 1.0) {
        return;
    }
    setTransformationAnchor(anchorUnderMouse ? QGraphicsView::AnchorUnderMouse
                                             : QGraphicsView::AnchorViewCenter);
    scale(factor, factor);
    emit viewChanged();
}

void CanvasView::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    if (steps != 0.0) {
        applyZoom(std::pow(kZoomStep, steps), true);
    }
    event->accept();
}

void CanvasView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && freeformDrawMode_) {
        freeformActive_ = true;
        emit freeformPointMm(mapToScene(event->position().toPoint()));
        QGraphicsView::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton && bezierDrawMode_) {
        bezierPressActive_ = true;
        bezierAnchorMm_ = mapToScene(event->position().toPoint());
        QGraphicsView::mousePressEvent(event);
        return;
    }
    // Un clic sur un élément interactif (poignée de nœud) ne doit pas
    // déclencher la sélection : la scène serait reconstruite en plein drag.
    QGraphicsItem* item = itemAt(event->position().toPoint());
    const bool onInteractiveItem =
        item != nullptr && (item->flags() & QGraphicsItem::ItemIsMovable);
    if (event->button() == Qt::LeftButton && !cropMode_ && !boxDrawMode_ && !onInteractiveItem) {
        emit canvasClickedMm(mapToScene(event->position().toPoint()));
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !cropMode_ && !boxDrawMode_) {
        emit canvasDoubleClickedMm(mapToScene(event->position().toPoint()));
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void CanvasView::contextMenuEvent(QContextMenuEvent* event) {
    if (cropMode_ || boxDrawMode_ || polygonDrawMode_ || freeformDrawMode_ || satinPairDrawMode_ ||
        bezierDrawMode_) {
        return;  // pas de menu contextuel pendant un recadrage/dessin
    }
    emit canvasContextMenu(mapToScene(event->pos()), event->globalPos());
    event->accept();
}

void CanvasView::mouseMoveEvent(QMouseEvent* event) {
    QGraphicsView::mouseMoveEvent(event);
    const QPointF scenePos = mapToScene(event->position().toPoint());
    // Passage scène (Y bas) -> repère physique (Y haut).
    emit cursorMovedMm(QPointF(scenePos.x(), -scenePos.y()));
    if (freeformActive_) {
        emit freeformPointMm(scenePos);
    }
    if (bezierPressActive_) {
        emit bezierPointDraggingMm(bezierAnchorMm_, scenePos);
    }
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event) {
    // Capturé AVANT QGraphicsView::mouseReleaseEvent (qui peut, selon le mode
    // de glisser, déclencher un traitement interne) : les modificateurs de CET
    // évènement, pas une relecture différée de QGuiApplication::keyboardModifiers()
    // (défaut trouvé par revue — pas fiable à rejouer dans un test QTest
    // offscreen, cf. Maj = cercle).
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    QGraphicsView::mouseReleaseEvent(event);
    if (freeformActive_ && event->button() == Qt::LeftButton) {
        freeformActive_ = false;
        emit freeformStrokeFinished();
    }
    if (bezierPressActive_ && event->button() == Qt::LeftButton) {
        bezierPressActive_ = false;
        emit bezierPointCommittedMm(bezierAnchorMm_, mapToScene(event->position().toPoint()));
    }
    if ((cropMode_ || boxDrawMode_) && lastRubberBandMm_.isValid() && !lastRubberBandMm_.isEmpty()) {
        const QRectF rect = lastRubberBandMm_;
        lastRubberBandMm_ = QRectF();
        if (cropMode_) {
            emit cropSelectedMm(rect);
        } else {
            emit boxDrawnMm(rect, modifiers);
        }
    }
}

void CanvasView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    emit viewChanged();
}

void CanvasView::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    emit viewChanged();
}

void CanvasView::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);

    // Zone du canevas en blanc.
    const QRectF canvas(-canvasMm_.width() / 2.0, -canvasMm_.height() / 2.0, canvasMm_.width(),
                        canvasMm_.height());
    painter->fillRect(canvas, Qt::white);

    // Grille adaptative : pas fin >= 8 px à l'écran, pas majeur = 5x ou 10x.
    const double pxPerMm = pixelsPerMm();
    const double fine = niceStepMm(pxPerMm, 8.0);
    const double major = niceStepMm(pxPerMm, 40.0);

    const auto drawGrid = [&](double step, const QColor& color) {
        QPen pen(color);
        pen.setCosmetic(true);
        painter->setPen(pen);
        const double x0 = std::floor(rect.left() / step) * step;
        const double y0 = std::floor(rect.top() / step) * step;
        for (double x = x0; x <= rect.right(); x += step) {
            painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
        }
        for (double y = y0; y <= rect.bottom(); y += step) {
            painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
        }
    };
    const QColor gridMajor = AppTheme::instance().tokens().canvasGrid;
    QColor gridFine = gridMajor;
    gridFine.setAlpha(gridMajor.alpha() / 2);
    drawGrid(fine, gridFine);
    drawGrid(major, gridMajor);

    // Axes du repère (origine au centre du canevas).
    QPen axisPen(AppTheme::instance().tokens().canvasAxis);
    axisPen.setCosmetic(true);
    painter->setPen(axisPen);
    painter->drawLine(QLineF(rect.left(), 0.0, rect.right(), 0.0));
    painter->drawLine(QLineF(0.0, rect.top(), 0.0, rect.bottom()));
}

void CanvasView::drawForeground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawForeground(painter, rect);

    // Cadre (limite physique de broderie), toujours visible au-dessus du contenu.
    const QRectF canvas(-canvasMm_.width() / 2.0, -canvasMm_.height() / 2.0, canvasMm_.width(),
                        canvasMm_.height());
    QPen pen(AppTheme::instance().tokens().canvasHoop);
    pen.setCosmetic(true);
    pen.setWidth(2);
    pen.setStyle(Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(canvas);
}

}  // namespace openstitch::desktop
