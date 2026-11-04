// SPDX-License-Identifier: Apache-2.0
#include "preview_canvas.hpp"

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QPixmap>
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

void PreviewCanvas::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (item_ != nullptr && !userZoomed_) {
        fitAll();
    }
}

} // namespace openstitch::desktop_v2
