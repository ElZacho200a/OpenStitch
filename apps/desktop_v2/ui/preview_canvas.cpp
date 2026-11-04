// SPDX-License-Identifier: Apache-2.0
#include "preview_canvas.hpp"

#include <QContextMenuEvent>
#include <QGraphicsItemGroup>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRubberBand>
#include <QWheelEvent>
#include <algorithm>
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
    scene_->clear(); // détruit aussi cadres, poignées, cadre de broderie et polygone en cours
    boxItems_.clear();
    item_ = nullptr;
    hoopItem_ = nullptr;
    gridItem_ = nullptr;
    measureLine_ = nullptr;
    measureText_ = nullptr;
    measuring_ = false;
    polyItem_ = nullptr;
    selGroup_ = nullptr;
    scaleOutline_ = nullptr;
    polyPts_.clear();
    unionPx_ = QRectF();
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
    if (mode != Mode::Measure) {
        clearMeasure();
    }
    setCursor(mode == Mode::DrawBox || mode == Mode::DrawPolygon || mode == Mode::Measure ||
                      mode == Mode::Pick
                  ? Qt::CrossCursor
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
    if (mode_ == Mode::Select) {
        const auto mods = event->modifiers() & ~Qt::KeypadModifier;
        if (mods == Qt::NoModifier || mods == Qt::ShiftModifier) {
            int sx = 0;
            int sy = 0;
            switch (event->key()) {
            case Qt::Key_Left:
                sx = -1;
                break;
            case Qt::Key_Right:
                sx = 1;
                break;
            case Qt::Key_Up:
                sy = 1;
                break;
            case Qt::Key_Down:
                sy = -1;
                break;
            default:
                break;
            }
            if (sx != 0 || sy != 0) {
                emit nudgeRequested(sx, sy, mods == Qt::ShiftModifier);
                event->accept();
                return;
            }
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
    if (selGroup_ != nullptr) {
        scene_->removeItem(selGroup_);
        delete selGroup_; // détruit aussi ses enfants
        selGroup_ = nullptr;
    }
    boxItems_.clear();
    unionPx_ = QRectF();
    if (item_ == nullptr || boxesPx.isEmpty()) {
        return;
    }
    selGroup_ = scene_->createItemGroup({});
    selGroup_->setZValue(10);
    const QPen pen = cosmeticPen(QColor(255, 64, 160), Qt::DashLine, 2);
    for (const QRectF& box : boxesPx) {
        auto* rect = new QGraphicsRectItem(box);
        rect->setPen(pen);
        selGroup_->addToGroup(rect);
        boxItems_.push_back(rect);
        unionPx_ = unionPx_.isNull() ? box : unionPx_.united(box);
    }
    // Poignées de mise à l'échelle aux quatre coins de l'emprise commune (taille constante à
    // l'écran).
    const QPointF corners[4] = {unionPx_.topLeft(), unionPx_.topRight(), unionPx_.bottomRight(),
                                unionPx_.bottomLeft()};
    for (const QPointF& c : corners) {
        auto* handle = new QGraphicsRectItem(-4, -4, 8, 8);
        handle->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        handle->setBrush(QColor(255, 255, 255));
        handle->setPen(cosmeticPen(QColor(255, 64, 160), Qt::SolidLine, 2));
        handle->setPos(c);
        selGroup_->addToGroup(handle);
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
    hoopItem_->setVisible(hoopVisible_);
}

void PreviewCanvas::setHoopVisible(bool visible) {
    hoopVisible_ = visible;
    if (hoopItem_ != nullptr) {
        hoopItem_->setVisible(visible);
    }
}

void PreviewCanvas::setGrid(const QVector<double>& xsPx, const QVector<double>& ysPx,
                            const QRectF& imageRect) {
    if (gridItem_ != nullptr) {
        scene_->removeItem(gridItem_);
        delete gridItem_;
        gridItem_ = nullptr;
    }
    if (item_ == nullptr || (xsPx.isEmpty() && ysPx.isEmpty())) {
        return;
    }
    QPainterPath path;
    for (const double x : xsPx) {
        path.moveTo(x, imageRect.top());
        path.lineTo(x, imageRect.bottom());
    }
    for (const double y : ysPx) {
        path.moveTo(imageRect.left(), y);
        path.lineTo(imageRect.right(), y);
    }
    gridItem_ = scene_->addPath(path, cosmeticPen(QColor(90, 110, 140, 110), Qt::SolidLine, 1));
    gridItem_->setZValue(4);
}

void PreviewCanvas::showMeasure(const QPointF& a, const QPointF& b, const QString& text) {
    clearMeasure();
    if (item_ == nullptr) {
        return;
    }
    measureLine_ = scene_->addLine(QLineF(a, b), cosmeticPen(QColor(0, 150, 255), Qt::DashLine, 2));
    measureLine_->setZValue(13);
    measureText_ = scene_->addSimpleText(text);
    measureText_->setBrush(QColor(0, 120, 220));
    measureText_->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    measureText_->setPos((a + b) / 2.0);
    measureText_->setZValue(14);
}

void PreviewCanvas::clearMeasure() {
    for (QGraphicsItem* it :
         {static_cast<QGraphicsItem*>(measureLine_), static_cast<QGraphicsItem*>(measureText_)}) {
        if (it != nullptr) {
            scene_->removeItem(it);
            delete it;
        }
    }
    measureLine_ = nullptr;
    measureText_ = nullptr;
}

void PreviewCanvas::contextMenuEvent(QContextMenuEvent* event) {
    if (item_ != nullptr && mode_ == Mode::Select) {
        emit contextMenuRequested(event->globalPos(), mapToScene(event->pos()));
        event->accept();
        return;
    }
    QGraphicsView::contextMenuEvent(event);
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
    if (mode_ == Mode::Measure && pressing_ && item_ != nullptr) {
        clearMeasure();
        measureFrom_ = mapToScene(event->pos());
        measuring_ = true;
        event->accept();
        return;
    }
    const bool plain = !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
    if (mode_ == Mode::Select && pressing_ && plain && item_ != nullptr) {
        const QPointF scenePos = mapToScene(event->pos());
        // 1) poignée de coin ? (8 pixels d'écran de tolérance)
        if (selGroup_ != nullptr && !unionPx_.isNull()) {
            const QPointF corners[4] = {unionPx_.topLeft(), unionPx_.topRight(),
                                        unionPx_.bottomRight(), unionPx_.bottomLeft()};
            for (int i = 0; i < 4; ++i) {
                if ((mapFromScene(corners[i]) - event->pos()).manhattanLength() <= 8) {
                    scaleHandle_ = i;
                    scaleCorner_ = corners[i];
                    scalePivot_ = corners[(i + 2) % 4]; // le coin opposé reste fixe
                    event->accept();
                    return;
                }
            }
        }
        // 2) objet sous le curseur : déplacement de la sélection
        if (moveHit_ && moveHit_(scenePos)) {
            emit moveStartRequested(scenePos); // la fenêtre l'ajoute à la sélection si besoin
            moving_ = true;
            moveOrigin_ = scenePos;
            event->accept();
            return;
        }
    }
    if ((mode_ == Mode::Select || mode_ == Mode::DrawBox) && pressing_) {
        bandOrigin_ = event->pos();
        bandDragging_ = true;
    }
    QGraphicsView::mousePressEvent(event);
}

void PreviewCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (measuring_) {
        const QPointF cur = mapToScene(event->pos());
        if (measureLine_ == nullptr) {
            measureLine_ = scene_->addLine(QLineF(measureFrom_, cur),
                                           cosmeticPen(QColor(0, 150, 255), Qt::DashLine, 2));
            measureLine_->setZValue(13);
        } else {
            measureLine_->setLine(QLineF(measureFrom_, cur));
        }
        event->accept();
        return;
    }
    if (moving_) {
        if (selGroup_ != nullptr) {
            selGroup_->setPos(mapToScene(event->pos()) - moveOrigin_); // retour visuel immédiat
        }
        event->accept();
        return;
    }
    if (scaleHandle_ >= 0) {
        const QPointF cur = mapToScene(event->pos());
        const double d0 =
            std::hypot(scaleCorner_.x() - scalePivot_.x(), scaleCorner_.y() - scalePivot_.y());
        const double d1 = std::hypot(cur.x() - scalePivot_.x(), cur.y() - scalePivot_.y());
        const double factor = d0 > 0.0 ? d1 / d0 : 1.0;
        const QRectF scaled(scalePivot_ + (unionPx_.topLeft() - scalePivot_) * factor,
                            scalePivot_ + (unionPx_.bottomRight() - scalePivot_) * factor);
        if (scaleOutline_ == nullptr) {
            scaleOutline_ =
                scene_->addRect(scaled, cosmeticPen(QColor(255, 64, 160), Qt::SolidLine, 2));
            scaleOutline_->setZValue(12);
        } else {
            scaleOutline_->setRect(scaled.normalized());
        }
        event->accept();
        return;
    }
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
    if (measuring_ && event->button() == Qt::LeftButton) {
        measuring_ = false;
        pressing_ = false;
        const QPointF end = mapToScene(event->pos());
        if ((event->pos() - mapFromScene(measureFrom_)).manhattanLength() > 4) {
            emit measuredPx(measureFrom_, end);
        } else {
            clearMeasure();
        }
        event->accept();
        return;
    }
    const bool leftRelease = pressing_ && event->button() == Qt::LeftButton;
    const bool click = leftRelease && (event->pos() - pressPos_).manhattanLength() <= 4;
    pressing_ = false;
    if (scaleHandle_ >= 0 && event->button() == Qt::LeftButton) {
        const QPointF cur = mapToScene(event->pos());
        const double d0 =
            std::hypot(scaleCorner_.x() - scalePivot_.x(), scaleCorner_.y() - scalePivot_.y());
        const double d1 = std::hypot(cur.x() - scalePivot_.x(), cur.y() - scalePivot_.y());
        scaleHandle_ = -1;
        if (scaleOutline_ != nullptr) {
            scene_->removeItem(scaleOutline_);
            delete scaleOutline_;
            scaleOutline_ = nullptr;
        }
        if (!click && d0 > 0.0) {
            emit scaleDraggedPx(scalePivot_, d1 / d0);
        }
        event->accept();
        return;
    }
    if (moving_ && event->button() == Qt::LeftButton) {
        moving_ = false;
        const QPointF delta = mapToScene(event->pos()) - moveOrigin_;
        if (selGroup_ != nullptr) {
            selGroup_->setPos(click ? QPointF() : delta); // un simple clic ne déplace rien
        }
        if (!click) {
            emit moveDraggedPx(delta);
            event->accept();
            return;
        }
        // Clic sans glisser : retombe sur la sélection au clic.
        emit clickedPx(mapToScene(event->pos()), event->modifiers());
        event->accept();
        return;
    }
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
        } else if (mode_ == Mode::Select || mode_ == Mode::Pick) {
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
