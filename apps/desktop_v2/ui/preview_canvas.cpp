// SPDX-License-Identifier: Apache-2.0
#include "preview_canvas.hpp"

#include <QContextMenuEvent>
#include <QFocusEvent>
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
#include <QScrollBar>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace openstitch::desktop_v2 {

namespace {
constexpr double kMinCanvasZoom = 0.05;
constexpr double kMaxCanvasZoom = 32.0;

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
    selectionFeedback_ = new QVariantAnimation(this);
    selectionFeedback_->setDuration(140);
    selectionFeedback_->setStartValue(0.65);
    selectionFeedback_->setEndValue(1.0);
    selectionFeedback_->setEasingCurve(QEasingCurve::OutCubic);
    connect(selectionFeedback_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        if (selGroup_ != nullptr) {
            selGroup_->setOpacity(v.toReal());
        }
    });
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
    selectionFeedback_->stop();
    lastSelectionBoxes_.clear();
    scene_->clear(); // détruit aussi cadres, poignées, cadre de broderie et polygone en cours
    boxItems_.clear();
    item_ = nullptr;
    hoopItem_ = nullptr;
    pointItems_.clear();
    sourceItem_ = nullptr;
    detailItem_ = nullptr;
    densityItem_ = nullptr;
    fabricItem_ = nullptr;
    fabricArrows_ = nullptr;
    snapGuides_ = nullptr;
    nodeItems_.clear();
    nodePts_.clear();
    nodeOutline_ = nullptr;
    nodeGuides_ = nullptr;
    nodeDragIndex_ = -1;
    nodeDragActive_ = false;
    strokeItem_ = nullptr;
    strokePts_.clear();
    stroking_ = false;
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
    spaceDragging_ = false;
    middlePanning_ = false;
    updateNavigationCursor();
}

void PreviewCanvas::fitAll() {
    if (item_ == nullptr) {
        return;
    }
    fitInView(item_, Qt::KeepAspectRatio);
    userZoomed_ = false;
}

void PreviewCanvas::fitSelection() {
    if (item_ == nullptr || unionPx_.isEmpty()) {
        return;
    }
    // Un peu d'air autour de la sélection ; les très petits objets ne deviennent pas
    // des pixels géants. Le cadrage reste indépendant du moteur et du type de point.
    const QRectF bounds = unionPx_.adjusted(-24, -24, 24, 24);
    fitInView(bounds, Qt::KeepAspectRatio);
    if (zoom() > kMaxCanvasZoom) {
        scale(kMaxCanvasZoom / zoom(), kMaxCanvasZoom / zoom());
        centerOn(bounds.center());
    }
    userZoomed_ = true;
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
    if (mode != Mode::Measure && mode != Mode::Cut) {
        clearMeasure();
    }
    updateNavigationCursor();
}

void PreviewCanvas::updateNavigationCursor() {
    const bool panning = mode_ == Mode::Pan || spacePanning_ || spaceDragging_;
    setDragMode(panning ? QGraphicsView::ScrollHandDrag : QGraphicsView::NoDrag);
    if (middlePanning_) {
        viewport()->setCursor(Qt::ClosedHandCursor);
    } else if (panning) {
        viewport()->setCursor(Qt::OpenHandCursor);
    } else if (mode_ == Mode::Select) {
        viewport()->setCursor(Qt::ArrowCursor);
    } else {
        viewport()->setCursor(Qt::CrossCursor);
    }
}

void PreviewCanvas::wheelEvent(QWheelEvent* event) {
    if (item_ == nullptr) {
        event->ignore();
        return;
    }
    // Trackpads haute résolution : pixelDelta est prioritaire ; une molette ordinaire
    // fournit angleDelta. Garder le pixel sous le curseur immobile pendant le zoom.
    const double delta = event->pixelDelta().y() != 0
                             ? static_cast<double>(event->pixelDelta().y()) * 2.0
                             : static_cast<double>(event->angleDelta().y());
    if (delta == 0.0) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    const QPointF before = mapToScene(event->position().toPoint());
    const double step = std::pow(1.0015, delta);
    const double actual = std::clamp(zoom() * step, kMinCanvasZoom, kMaxCanvasZoom) / zoom();
    scale(actual, actual);
    const QPointF after = mapToScene(event->position().toPoint());
    translate(after.x() - before.x(), after.y() - before.y());
    userZoomed_ = true;
    event->accept();
}

void PreviewCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) {
            spacePanning_ = true;
            updateNavigationCursor();
        }
        event->accept();
        return;
    }
    if (nodeDragIndex_ >= 0 && event->key() == Qt::Key_Escape) {
        endNodeDrag();
        emit nodeDragCancelled();
        event->accept();
        return;
    }
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
    if (event->key() == Qt::Key_F) {
        if (event->modifiers() == Qt::ShiftModifier) {
            fitSelection();
            event->accept();
            return;
        }
        if (event->modifiers() == Qt::NoModifier) {
            fitAll();
            event->accept();
            return;
        }
    }
    if (item_ != nullptr && event->modifiers() == Qt::NoModifier) {
        const bool zoomIn = event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal;
        const bool zoomOut = event->key() == Qt::Key_Minus || event->key() == Qt::Key_Underscore;
        if (zoomIn || zoomOut) {
            const QPointF centre = mapToScene(viewport()->rect().center());
            const double factor = zoomIn ? 1.25 : 0.8;
            const double actual =
                std::clamp(zoom() * factor, kMinCanvasZoom, kMaxCanvasZoom) / zoom();
            scale(actual, actual);
            centerOn(centre);
            userZoomed_ = true;
            event->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}

void PreviewCanvas::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) {
            spacePanning_ = false;
            if (!spaceDragging_) {
                updateNavigationCursor();
            }
        }
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void PreviewCanvas::focusOutEvent(QFocusEvent* event) {
    spacePanning_ = false;
    if (!spaceDragging_) {
        updateNavigationCursor();
    }
    QGraphicsView::focusOutEvent(event);
}

void PreviewCanvas::setSelectionBoxes(const QVector<QRectF>& boxesPx) {
    // No flashing on repeated refreshes of the same selection or old rendered frames.
    if (boxesPx == lastSelectionBoxes_ && selGroup_ != nullptr) {
        return;
    }
    selectionFeedback_->stop();
    lastSelectionBoxes_ = boxesPx;
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
    // Feedback lives in the Qt overlay only. No stitch regeneration or project mutation.
    if (!reducedMotion_ && boxesPx.size() <= 20) {
        selGroup_->setOpacity(0.65);
        selectionFeedback_->start();
    }
}

void PreviewCanvas::setReducedMotion(bool reduced) {
    reducedMotion_ = reduced;
    if (reduced) {
        selectionFeedback_->stop();
        if (selGroup_ != nullptr) {
            selGroup_->setOpacity(1.0);
        }
    }
}

bool PreviewCanvas::selectionFeedbackActive() const {
    return selectionFeedback_->state() == QAbstractAnimation::Running;
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

void PreviewCanvas::setSourceImage(const QImage& image, const QRectF& rectPx) {
    if (sourceItem_ != nullptr) {
        scene_->removeItem(sourceItem_);
        delete sourceItem_;
        sourceItem_ = nullptr;
    }
    if (item_ == nullptr || image.isNull()) {
        return;
    }
    sourceItem_ = scene_->addPixmap(QPixmap::fromImage(image));
    sourceItem_->setTransformationMode(Qt::SmoothTransformation);
    sourceItem_->setOpacity(0.55);
    sourceItem_->setZValue(3);
    // L'image est posée à `rectPx` : mise à l'échelle depuis ses pixels propres.
    sourceItem_->setTransform(
        QTransform::fromScale(rectPx.width() / image.width(), rectPx.height() / image.height()));
    sourceItem_->setPos(rectPx.topLeft());
    sourceItem_->setVisible(sourceVisible_);
    // La scène doit contenir l'image même quand elle déborde de l'aperçu (clic hors des points).
    scene_->setSceneRect(item_->boundingRect().united(rectPx).adjusted(-40, -40, 40, 40));
}

void PreviewCanvas::setPointMarkers(const QVector<QPointF>& scenePx, int selected) {
    for (QGraphicsItem* it : pointItems_) {
        scene_->removeItem(it);
        delete it;
    }
    pointItems_.clear();
    if (item_ == nullptr) {
        return;
    }
    pointItems_.reserve(scenePx.size());
    for (int i = 0; i < scenePx.size(); ++i) {
        const bool sel = i == selected;
        const double r = sel ? 4.0 : 2.5;
        auto* dot = scene_->addEllipse(
            -r, -r, 2 * r, 2 * r,
            cosmeticPen(sel ? QColor(230, 60, 60) : QColor(20, 110, 200), Qt::SolidLine, 1),
            QBrush(sel ? QColor(255, 200, 60) : QColor(255, 255, 255)));
        dot->setPos(scenePx[i]);
        dot->setZValue(sel ? 16 : 15);
        dot->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        pointItems_.push_back(dot);
    }
}

void PreviewCanvas::setSourceVisible(bool visible) {
    sourceVisible_ = visible;
    if (sourceItem_ != nullptr) {
        sourceItem_->setVisible(visible);
    }
    if (detailItem_ != nullptr) {
        detailItem_->setVisible(visible);
    }
}

void PreviewCanvas::setDetailOverlay(const QImage& image, const QRectF& rectPx) {
    if (detailItem_ != nullptr) {
        scene_->removeItem(detailItem_);
        delete detailItem_;
        detailItem_ = nullptr;
    }
    if (item_ == nullptr || image.isNull()) {
        return;
    }
    detailItem_ = scene_->addPixmap(QPixmap::fromImage(image));
    detailItem_->setZValue(3.5);
    detailItem_->setTransform(
        QTransform::fromScale(rectPx.width() / image.width(), rectPx.height() / image.height()));
    detailItem_->setPos(rectPx.topLeft());
    detailItem_->setVisible(sourceVisible_);
}

void PreviewCanvas::setDensityOverlay(const QImage& image, const QRectF& rectPx) {
    if (densityItem_ != nullptr) {
        scene_->removeItem(densityItem_);
        delete densityItem_;
        densityItem_ = nullptr;
    }
    if (item_ == nullptr || image.isNull()) {
        return;
    }
    densityItem_ = scene_->addPixmap(QPixmap::fromImage(image));
    densityItem_->setTransformationMode(Qt::FastTransformation); // cases nettes, pas lissées
    densityItem_->setZValue(3.2);
    densityItem_->setTransform(
        QTransform::fromScale(rectPx.width() / image.width(), rectPx.height() / image.height()));
    densityItem_->setPos(rectPx.topLeft());
    densityItem_->setAcceptedMouseButtons(Qt::NoButton); // ne gêne ni la sélection ni les outils
}

void PreviewCanvas::setNodeHandles(const QVector<QPointF>& scenePx, const QVector<int>& selected) {
    for (QGraphicsItem* it : nodeItems_) {
        scene_->removeItem(it);
        delete it;
    }
    nodeItems_.clear();
    nodePts_.clear();
    if (item_ == nullptr) {
        return;
    }
    nodePts_ = scenePx;
    nodeItems_.reserve(scenePx.size());
    for (int i = 0; i < scenePx.size(); ++i) {
        const bool sel = selected.contains(i);
        const double r = sel ? 5.0 : 4.0;
        auto* square = scene_->addRect(
            -r, -r, 2 * r, 2 * r,
            cosmeticPen(sel ? QColor(169, 45, 102) : QColor(40, 44, 52), Qt::SolidLine, 1),
            QBrush(sel ? QColor(169, 45, 102) : QColor(255, 255, 255)));
        square->setPos(scenePx[i]);
        square->setZValue(sel ? 18 : 17);
        square->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        square->setAcceptedMouseButtons(Qt::NoButton);
        nodeItems_.push_back(square);
    }
}

void PreviewCanvas::setNodeOutline(const QVector<QVector<QPointF>>& polylinesPx, bool valid) {
    if (nodeOutline_ != nullptr) {
        scene_->removeItem(nodeOutline_);
        delete nodeOutline_;
        nodeOutline_ = nullptr;
    }
    if (item_ == nullptr || polylinesPx.isEmpty()) {
        return;
    }
    QPainterPath path;
    for (const auto& line : polylinesPx) {
        if (line.isEmpty()) {
            continue;
        }
        path.moveTo(line.first());
        for (int i = 1; i < line.size(); ++i) {
            path.lineTo(line[i]);
        }
    }
    nodeOutline_ =
        scene_->addPath(path, cosmeticPen(valid ? QColor(169, 45, 102) : QColor(200, 40, 40),
                                          valid ? Qt::SolidLine : Qt::DashLine, 2));
    nodeOutline_->setZValue(16);
    nodeOutline_->setAcceptedMouseButtons(Qt::NoButton);
}

void PreviewCanvas::setFabricOverlay(const QImage& image, const QRectF& rectPx) {
    if (fabricItem_ != nullptr) {
        scene_->removeItem(fabricItem_);
        delete fabricItem_;
        fabricItem_ = nullptr;
    }
    if (item_ == nullptr || image.isNull()) {
        return;
    }
    fabricItem_ = scene_->addPixmap(QPixmap::fromImage(image));
    fabricItem_->setTransformationMode(Qt::FastTransformation);
    fabricItem_->setZValue(3.3);
    fabricItem_->setTransform(
        QTransform::fromScale(rectPx.width() / image.width(), rectPx.height() / image.height()));
    fabricItem_->setPos(rectPx.topLeft());
    fabricItem_->setAcceptedMouseButtons(Qt::NoButton);
}

void PreviewCanvas::setFabricArrows(const QVector<QLineF>& linesPx) {
    if (fabricArrows_ != nullptr) {
        scene_->removeItem(fabricArrows_);
        delete fabricArrows_;
        fabricArrows_ = nullptr;
    }
    if (item_ == nullptr || linesPx.isEmpty()) {
        return;
    }
    QPainterPath path;
    for (const QLineF& line : linesPx) {
        path.moveTo(line.p1());
        path.lineTo(line.p2());
        // Pointe de flèche : deux petits traits à 150° de part et d'autre de la direction.
        const double len = line.length();
        if (len > 0.5) {
            const QLineF back(line.p2(), line.p1());
            for (const double angle : {25.0, -25.0}) {
                QLineF head = back;
                head.setLength(std::min(len * 0.35, 3.0));
                head.setAngle(head.angle() + angle);
                path.moveTo(line.p2());
                path.lineTo(head.p2());
            }
        }
    }
    fabricArrows_ = scene_->addPath(path, cosmeticPen(QColor(20, 90, 200, 220), Qt::SolidLine, 1));
    fabricArrows_->setZValue(3.4);
    fabricArrows_->setAcceptedMouseButtons(Qt::NoButton);
}

void PreviewCanvas::setSnapGuides(const QVector<QLineF>& linesPx) {
    if (snapGuides_ != nullptr) {
        scene_->removeItem(snapGuides_);
        delete snapGuides_;
        snapGuides_ = nullptr;
    }
    if (item_ == nullptr || linesPx.isEmpty()) {
        return;
    }
    QPainterPath path;
    for (const QLineF& line : linesPx) {
        path.moveTo(line.p1());
        path.lineTo(line.p2());
    }
    snapGuides_ = scene_->addPath(path, cosmeticPen(QColor(230, 40, 140), Qt::DashLine, 1));
    snapGuides_->setZValue(19);
    snapGuides_->setAcceptedMouseButtons(Qt::NoButton);
}

void PreviewCanvas::setNodeGuides(const QVector<QLineF>& linesPx) {
    if (nodeGuides_ != nullptr) {
        scene_->removeItem(nodeGuides_);
        delete nodeGuides_;
        nodeGuides_ = nullptr;
    }
    if (item_ == nullptr || linesPx.isEmpty()) {
        return;
    }
    QPainterPath path;
    for (const QLineF& line : linesPx) {
        path.moveTo(line.p1());
        path.lineTo(line.p2());
    }
    nodeGuides_ = scene_->addPath(path, cosmeticPen(QColor(110, 118, 135, 150), Qt::SolidLine, 1));
    nodeGuides_->setZValue(15.5);
    nodeGuides_->setAcceptedMouseButtons(Qt::NoButton);
}

int PreviewCanvas::nodeAt(const QPoint& viewPos) const {
    int best = -1;
    int bestDist = 10; // pixels de vue : zone de capture confortable
    for (int i = 0; i < nodePts_.size(); ++i) {
        const int d = (mapFromScene(nodePts_[i]) - viewPos).manhattanLength();
        if (d <= bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

void PreviewCanvas::endNodeDrag() {
    nodeDragIndex_ = -1;
    nodeDragActive_ = false;
    pressing_ = false;
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
    if (event->button() == Qt::MiddleButton) {
        middlePanning_ = true;
        middlePanPos_ = event->pos();
        updateNavigationCursor();
        event->accept();
        return;
    }
    if (spacePanning_ && event->button() == Qt::LeftButton) {
        spaceDragging_ = true;
        updateNavigationCursor();
        QGraphicsView::mousePressEvent(event);
        event->accept();
        return;
    }
    pressing_ = event->button() == Qt::LeftButton;
    pressPos_ = event->pos();
    if (mode_ == Mode::Stroke && pressing_ && item_ != nullptr) {
        stroking_ = true;
        strokePts_ = {mapToScene(event->pos())};
        event->accept();
        return;
    }
    if ((mode_ == Mode::Measure || mode_ == Mode::Cut) && pressing_ && item_ != nullptr) {
        clearMeasure();
        measureFrom_ = mapToScene(event->pos());
        measuring_ = true;
        event->accept();
        return;
    }
    if (mode_ == Mode::Nodes && pressing_ && item_ != nullptr) {
        const int index = nodeAt(event->pos());
        if (index >= 0) {
            emit nodePressed(index, event->modifiers());
            nodeDragIndex_ = index;
            nodeDragActive_ = false;
            nodeDragOrigin_ = mapToScene(event->pos());
            event->accept();
            return;
        }
        const QPointF scenePos = mapToScene(event->pos());
        if (segmentHit_ && segmentHit_(scenePos)) {
            emit segmentPressed(scenePos);
            nodeDragIndex_ = 1'000'000; // glisser d'un segment : même mécanique que les nœuds
            nodeDragActive_ = false;
            nodeDragOrigin_ = scenePos;
            event->accept();
            return;
        }
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
    if (middlePanning_) {
        const QPoint move = event->pos() - middlePanPos_;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - move.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - move.y());
        middlePanPos_ = event->pos();
        event->accept();
        return;
    }
    if (spaceDragging_) {
        QGraphicsView::mouseMoveEvent(event);
        event->accept();
        return;
    }
    if (nodeDragIndex_ >= 0) {
        if (!nodeDragActive_ &&
            (event->pos() - mapFromScene(nodeDragOrigin_)).manhattanLength() > 3) {
            nodeDragActive_ = true; // seuil anti-tremblement : un simple clic ne déplace rien
        }
        if (nodeDragActive_) {
            emit nodeDragMovedPx(mapToScene(event->pos()) - nodeDragOrigin_);
        }
        event->accept();
        return;
    }
    if (stroking_) {
        strokePts_.push_back(mapToScene(event->pos()));
        QPainterPath path;
        path.moveTo(strokePts_.first());
        for (int i = 1; i < strokePts_.size(); ++i) {
            path.lineTo(strokePts_[i]);
        }
        if (strokeItem_ == nullptr) {
            strokeItem_ =
                scene_->addPath(path, cosmeticPen(QColor(230, 60, 60, 200), Qt::SolidLine, 3));
            strokeItem_->setZValue(14);
        } else {
            strokeItem_->setPath(path);
        }
        event->accept();
        return;
    }
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
            QPointF delta = mapToScene(event->pos()) - moveOrigin_;
            if (moveSnap_) {
                delta = moveSnap_(delta); // magnétisme : le retour visuel montre l'accrochage
            }
            selGroup_->setPos(delta); // retour visuel immédiat
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
    if (middlePanning_ && event->button() == Qt::MiddleButton) {
        middlePanning_ = false;
        updateNavigationCursor();
        event->accept();
        return;
    }
    if (spaceDragging_ && event->button() == Qt::LeftButton) {
        QGraphicsView::mouseReleaseEvent(event);
        spaceDragging_ = false;
        updateNavigationCursor();
        event->accept();
        return;
    }
    if (nodeDragIndex_ >= 0 && event->button() == Qt::LeftButton) {
        const bool moved = nodeDragActive_;
        const QPointF delta = mapToScene(event->pos()) - nodeDragOrigin_;
        endNodeDrag();
        if (moved) {
            emit nodeDragFinishedPx(delta);
        }
        event->accept();
        return;
    }
    if (stroking_ && event->button() == Qt::LeftButton) {
        stroking_ = false;
        pressing_ = false;
        strokePts_.push_back(mapToScene(event->pos()));
        if (strokeItem_ != nullptr) {
            scene_->removeItem(strokeItem_);
            delete strokeItem_;
            strokeItem_ = nullptr;
        }
        const QVector<QPointF> done = strokePts_;
        strokePts_.clear();
        emit strokeDrawnPx(done, event->modifiers());
        event->accept();
        return;
    }
    if (measuring_ && event->button() == Qt::LeftButton) {
        measuring_ = false;
        pressing_ = false;
        const QPointF end = mapToScene(event->pos());
        if ((event->pos() - mapFromScene(measureFrom_)).manhattanLength() > 4) {
            if (mode_ == Mode::Cut) {
                const QPointF from = measureFrom_;
                clearMeasure();
                emit lineDrawnPx(from, end);
            } else {
                emit measuredPx(measureFrom_, end);
            }
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
        QPointF delta = mapToScene(event->pos()) - moveOrigin_;
        if (moveSnap_ && !click) {
            delta = moveSnap_(delta); // la valeur validée est celle qui était affichée
        }
        setSnapGuides({});
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
        } else if (mode_ == Mode::Select || mode_ == Mode::Pick || mode_ == Mode::Nodes) {
            emit clickedPx(mapToScene(event->pos()), event->modifiers());
        }
    }
}

void PreviewCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (mode_ == Mode::Nodes && event->button() == Qt::LeftButton && item_ != nullptr &&
        nodeAt(event->pos()) < 0) {
        emit nodeInsertRequestedPx(mapToScene(event->pos()));
        event->accept();
        return;
    }
    if (!spacePanning_ && !spaceDragging_ && mode_ == Mode::DrawPolygon &&
        event->button() == Qt::LeftButton) {
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
