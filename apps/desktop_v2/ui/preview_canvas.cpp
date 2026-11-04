// SPDX-License-Identifier: Apache-2.0
#include "preview_canvas.hpp"

#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRubberBand>
#include <QWheelEvent>
#include <cmath>

namespace openstitch::desktop_v2 {

namespace {
QPen cosmeticPen(const QColor& colour, Qt::PenStyle style, int width) {
    QPen pen(colour);
    pen.setCosmetic(true); // épaisseur constante quel que soit le zoom
    pen.setWidth(width);
    pen.setStyle(style);
    return pen;
}
} // namespace

PreviewCanvas::PreviewCanvas(QWidget* parent)
    : QGraphicsView(parent), scene_(new QGraphicsScene(this)) {
    setScene(scene_);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::NoAnchor); // ancrage géré à la main (cf. wheelEvent)
    setResizeAnchor(QGraphicsView::NoAnchor);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true); // aperçu du segment en cours dans le mode polygone
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
    scene_->clear(); // détruit aussi cadres, cadre de broderie et polygone en cours
    boxItems_.clear();
    item_ = nullptr;
    hoopItem_ = nullptr;
    polyItem_ = nullptr;
    polyPts_.clear();
    userZoomed_ = false;
}

void PreviewCanvas::fitAll() {
    if (item_ == nullptr) {
        return;
    }
    fitInView(item_, Qt::KeepAspectRatio);
    userZoomed_ = false;
}

void PreviewCanvas::setMode(Mode mode) {
    if (mode_ == Mode::DrawPolygon && mode != Mode::DrawPolygon) {
        cancelPolygon();
    }
    mode_ = mode;
    bandDragging_ = false;
    if (band_ != nullptr) {
        band_->hide();
    }
    setDragMode(mode == Mode::Pan ? QGraphicsView::ScrollHandDrag : QGraphicsView::NoDrag);
    setCursor(mode == Mode::DrawBox || mode == Mode::DrawPolygon ? Qt::CrossCursor
                                                                 : Qt::ArrowCursor);
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
    if (mode_ == Mode::DrawPolygon) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            finishPolygon();
            event->accept();
            return;
        case Qt::Key_Escape:
            cancelPolygon();
            event->accept();
            return;
        case Qt::Key_Backspace:
            if (!polyPts_.isEmpty()) {
                polyPts_.removeLast();
                updatePolygonItem(nullptr);
            }
            event->accept();
            return;
        default:
            break;
        }
    }
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
    const QPen pen = cosmeticPen(QColor(255, 64, 160), Qt::DashLine, 2);
    for (const QRectF& box : boxesPx) {
        auto* rect = scene_->addRect(box, pen);
        rect->setZValue(10);
        boxItems_.push_back(rect);
    }
}

void PreviewCanvas::setHoopRect(const QRectF& rectPx) {
    if (hoopItem_ != nullptr) {
        scene_->removeItem(hoopItem_);
        delete hoopItem_;
        hoopItem_ = nullptr;
    }
    if (item_ == nullptr || rectPx.isNull()) {
        return;
    }
    hoopItem_ = scene_->addRect(rectPx, cosmeticPen(QColor(120, 120, 130), Qt::DashDotLine, 1));
    hoopItem_->setZValue(5);
}

void PreviewCanvas::updatePolygonItem(const QPointF* cursor) {
    if (polyItem_ == nullptr) {
        polyItem_ =
            scene_->addPath(QPainterPath(), cosmeticPen(QColor(255, 64, 160), Qt::SolidLine, 2));
        polyItem_->setZValue(11);
    }
    QPainterPath path;
    if (!polyPts_.isEmpty()) {
        path.moveTo(polyPts_.first());
        for (int i = 1; i < polyPts_.size(); ++i) {
            path.lineTo(polyPts_[i]);
        }
        if (cursor != nullptr) {
            path.lineTo(*cursor); // segment élastique jusqu'au curseur
        }
    }
    polyItem_->setPath(path);
}

void PreviewCanvas::finishPolygon() {
    if (polyPts_.size() >= 3) {
        const QVector<QPointF> done = polyPts_;
        cancelPolygon();
        emit polygonDrawnPx(done);
    }
}

void PreviewCanvas::cancelPolygon() {
    polyPts_.clear();
    if (polyItem_ != nullptr) {
        scene_->removeItem(polyItem_);
        delete polyItem_;
        polyItem_ = nullptr;
    }
}

void PreviewCanvas::mousePressEvent(QMouseEvent* event) {
    pressing_ = event->button() == Qt::LeftButton;
    pressPos_ = event->pos();
    if ((mode_ == Mode::Select || mode_ == Mode::DrawBox) && pressing_) {
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
    if (mode_ == Mode::DrawPolygon && !polyPts_.isEmpty()) {
        const QPointF cursor = mapToScene(event->pos());
        updatePolygonItem(&cursor);
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
            const QRectF px =
                mapToScene(QRect(bandOrigin_, event->pos()).normalized()).boundingRect();
            if (mode_ == Mode::DrawBox) {
                emit boxDrawnPx(px, event->modifiers());
            } else {
                emit rectPx(px, event->modifiers());
            }
            return;
        }
    }
    QGraphicsView::mouseReleaseEvent(event);
    if (click && item_ != nullptr) {
        if (mode_ == Mode::DrawPolygon) {
            polyPts_.push_back(mapToScene(event->pos()));
            updatePolygonItem(nullptr);
        } else if (mode_ == Mode::Select) {
            emit clickedPx(mapToScene(event->pos()), event->modifiers());
        }
    }
}

void PreviewCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (mode_ == Mode::DrawPolygon && event->button() == Qt::LeftButton) {
        finishPolygon(); // le premier clic du double-clic a déjà posé le dernier sommet
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void PreviewCanvas::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (item_ != nullptr && !userZoomed_) {
        fitAll();
    }
}

} // namespace openstitch::desktop_v2
