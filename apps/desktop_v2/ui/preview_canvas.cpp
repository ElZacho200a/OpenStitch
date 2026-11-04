// SPDX-License-Identifier: Apache-2.0
#include "preview_canvas.hpp"

#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPen>
#include <QPixmap>
#include <QRubberBand>
#include <QWheelEvent>
#include <cmath>

namespace openstitch::desktop_v2 {

PreviewCanvas::PreviewCanvas(QWidget* parent)
    : QGraphicsView(parent), scene_(new QGraphicsScene(this)) {
    setScene(scene_);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::NoAnchor); // ancrage géré à la main (cf. wheelEvent)
    setResizeAnchor(QGraphicsView::NoAnchor);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Canevas d'aperçu"));
}

void PreviewCanvas::setImage(const QImage& image) {
    const bool first = item_ == nullptr;
    if (item_ == nullptr) {
        item_ = scene_->addPixmap(QPixmap::fromImage(image));
        item_->setTransformationMode(Qt::SmoothTransformation);
    } else {
        item_->setPixmap(QPixmap::fromImage(image));
    }
    scene_->setSceneRect(item_->boundingRect().adjusted(-40, -40, 40, 40));
    if (first || !userZoomed_) {
        fitAll();
    }
}

void PreviewCanvas::clearImage() {
    scene_->clear();
    boxItems_.clear(); // détruits avec la scène
    item_ = nullptr;
    userZoomed_ = false;
}

void PreviewCanvas::fitAll() {
    if (item_ == nullptr) {
        return;
    }
    fitInView(item_, Qt::KeepAspectRatio);
    userZoomed_ = false;
}

void PreviewCanvas::wheelEvent(QWheelEvent* event) {
    if (item_ == nullptr) {
        return;
    }
    // Zoom ancré sous le curseur, calculé depuis la position de l'ÉVÉNEMENT (pas QCursor::pos()) :
    // correct avec des événements synthétiques (tests) et des pavés tactiles.
    const QPointF before = mapToScene(event->position().toPoint());
    const double step = std::pow(1.0015, event->angleDelta().y());
    scale(step, step);
    const QPointF after = mapToScene(event->position().toPoint());
    translate(after.x() - before.x(), after.y() - before.y());
    userZoomed_ = true;
    event->accept();
}

void PreviewCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F && event->modifiers() == Qt::NoModifier) {
        fitAll();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void PreviewCanvas::setSelectionBoxes(const QVector<QRectF>& boxesPx) {
    for (auto* item : boxItems_) {
        scene_->removeItem(item);
        delete item;
    }
    boxItems_.clear();
    if (item_ == nullptr) {
        return;
    }
    QPen pen(QColor(255, 64, 160));
    pen.setCosmetic(true); // épaisseur constante quel que soit le zoom
    pen.setWidth(2);
    pen.setStyle(Qt::DashLine);
    for (const QRectF& box : boxesPx) {
        auto* rect = scene_->addRect(box, pen);
        rect->setZValue(10);
        boxItems_.push_back(rect);
    }
}

void PreviewCanvas::setRubberBandSelection(bool on) {
    bandMode_ = on;
    bandDragging_ = false;
    if (band_ != nullptr) {
        band_->hide();
    }
    setDragMode(on ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
}

void PreviewCanvas::mousePressEvent(QMouseEvent* event) {
    pressing_ = event->button() == Qt::LeftButton;
    pressPos_ = event->pos();
    if (bandMode_ && pressing_) {
        bandOrigin_ = event->pos();
        bandDragging_ = true;
    }
    QGraphicsView::mousePressEvent(event);
}

void PreviewCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (bandDragging_) {
        if (band_ == nullptr) {
            band_ = new QRubberBand(QRubberBand::Rectangle, viewport());
        }
        band_->setGeometry(QRect(bandOrigin_, event->pos()).normalized());
        band_->show();
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void PreviewCanvas::mouseReleaseEvent(QMouseEvent* event) {
    const bool leftRelease = pressing_ && event->button() == Qt::LeftButton;
    const bool click = leftRelease && (event->pos() - pressPos_).manhattanLength() <= 4;
    pressing_ = false;
    if (bandDragging_ && event->button() == Qt::LeftButton) {
        bandDragging_ = false;
        if (band_ != nullptr) {
            band_->hide();
        }
        if (!click && item_ != nullptr) {
            const QRect px = QRect(bandOrigin_, event->pos()).normalized();
            emit rectPx(mapToScene(px).boundingRect(), event->modifiers());
            return;
        }
    }
    QGraphicsView::mouseReleaseEvent(event);
    if (click && item_ != nullptr) {
        emit clickedPx(mapToScene(event->pos()), event->modifiers());
    }
}

void PreviewCanvas::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (item_ != nullptr && !userZoomed_) {
        fitAll();
    }
}

} // namespace openstitch::desktop_v2
