// SPDX-License-Identifier: Apache-2.0
#include "canvas_view.hpp"

#include <QGraphicsItem>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

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
    setBackgroundBrush(QColor(235, 235, 238));
    // Rendu de gros motifs : ne pas sauvegarder l'état du peintre entre items,
    // et ne repeindre que la zone modifiée plutôt que tout le viewport.
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setCacheMode(QGraphicsView::CacheBackground);

    connect(this, &QGraphicsView::rubberBandChanged, this,
            [this](QRect viewportRect, QPointF fromScene, QPointF toScene) {
                if (!viewportRect.isNull()) {
                    lastRubberBandMm_ = QRectF(fromScene, toScene).normalized();
                }
            });
}

void CanvasView::setCropMode(bool enabled) {
    cropMode_ = enabled;
    lastRubberBandMm_ = QRectF();
    setDragMode(enabled ? QGraphicsView::RubberBandDrag : QGraphicsView::ScrollHandDrag);
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
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
    // Un clic sur un élément interactif (poignée de nœud) ne doit pas
    // déclencher la sélection : la scène serait reconstruite en plein drag.
    QGraphicsItem* item = itemAt(event->position().toPoint());
    const bool onInteractiveItem =
        item != nullptr && (item->flags() & QGraphicsItem::ItemIsMovable);
    if (event->button() == Qt::LeftButton && !cropMode_ && !onInteractiveItem) {
        emit canvasClickedMm(mapToScene(event->position().toPoint()));
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent* event) {
    QGraphicsView::mouseMoveEvent(event);
    const QPointF scenePos = mapToScene(event->position().toPoint());
    // Passage scène (Y bas) -> repère physique (Y haut).
    emit cursorMovedMm(QPointF(scenePos.x(), -scenePos.y()));
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event) {
    QGraphicsView::mouseReleaseEvent(event);
    if (cropMode_ && lastRubberBandMm_.isValid() && !lastRubberBandMm_.isEmpty()) {
        const QRectF rect = lastRubberBandMm_;
        lastRubberBandMm_ = QRectF();
        emit cropSelectedMm(rect);
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
    drawGrid(fine, QColor(0, 0, 0, 18));
    drawGrid(major, QColor(0, 0, 0, 40));

    // Axes du repère (origine au centre du canevas).
    QPen axisPen(QColor(120, 120, 160, 120));
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
    QPen pen(QColor(200, 60, 60));
    pen.setCosmetic(true);
    pen.setWidth(2);
    pen.setStyle(Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(canvas);
}

}  // namespace openstitch::desktop
