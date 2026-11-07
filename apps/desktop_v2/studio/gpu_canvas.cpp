// SPDX-License-Identifier: Apache-2.0
#include "gpu_canvas.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QWheelEvent>
#include <cmath>

#include "canvas_renderer.hpp"

namespace openstitch::desktop_v2::studio {
namespace {
QString apiName(QSGRendererInterface::GraphicsApi api) {
    switch (api) {
    case QSGRendererInterface::Direct3D11:
        return QStringLiteral("RHI · Direct3D 11 / Qt RHI");
    case QSGRendererInterface::Direct3D12:
        return QStringLiteral("RHI · Direct3D 12 / Qt RHI");
    case QSGRendererInterface::Vulkan:
        return QStringLiteral("RHI · Vulkan / Qt RHI");
    case QSGRendererInterface::Metal:
        return QStringLiteral("RHI · Metal / Qt RHI");
    case QSGRendererInterface::OpenGL:
        return QStringLiteral("RHI · OpenGL / Qt RHI");
    case QSGRendererInterface::Software:
        return QStringLiteral("CPU · Qt Quick software");
    case QSGRendererInterface::Null:
        return QStringLiteral("Qt RHI null · sans affichage GPU");
    default:
        return QStringLiteral("API graphique inconnue");
    }
}
} // namespace

GpuCanvas::GpuCanvas(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
    setFlag(ItemIsFocusScope);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setClip(true);
    setAntialiasing(true);
    setCursor(Qt::ArrowCursor);
}

void GpuCanvas::setController(StudioController* controller) {
    if (controller_ == controller) {
        return;
    }
    if (controller_) {
        disconnect(controller_, nullptr, this, nullptr);
    }
    controller_ = controller;
    fitPending_ = true;
    if (controller_) {
        connect(controller_, &StudioController::documentReplaced, this, [this] {
            fitPending_ = true;
            viewHistory_.clear();
        });
        connect(controller_, &StudioController::sceneChanged, this,
                [this] { setScene(controller_->scene()); });
        connect(controller_, &StudioController::selectionChanged, this,
                [this] { setSelection(controller_->session().selection().targets()); });
        connect(controller_, &QObject::destroyed, this, [this] {
            controller_ = nullptr;
            setScene(nullptr);
            setSelection({});
            emit controllerChanged();
        });
        setScene(controller_->scene());
        setSelection(controller_->session().selection().targets());
    } else {
        setScene(nullptr);
        setSelection({});
    }
    emit controllerChanged();
}

void GpuCanvas::setScene(std::shared_ptr<const StudioSceneSnapshot> scene) {
    if (scene_ == scene) {
        return;
    }
    scene_ = std::move(scene);
    if (fitPending_ && scene_) {
        fitDesign();
    }
    update();
}

void GpuCanvas::setSelection(std::vector<Target> selection) {
    if (selection == selection_) {
        return;
    }
    selection_ = std::move(selection);
    ++selectionRevision_;
    update();
}

void GpuCanvas::setPanMode(bool value) {
    if (value == panMode_) {
        return;
    }
    panMode_ = value;
    setCursor(value ? Qt::OpenHandCursor : Qt::ArrowCursor);
    emit panModeChanged();
}

void GpuCanvas::setOverlays(std::shared_ptr<const CanvasOverlays> overlays) {
    overlays_ = std::move(overlays);
    ++overlayRevision_;
    update();
}

void GpuCanvas::setZoomRectMode(bool value) {
    if (value == zoomRectMode_) {
        return;
    }
    zoomRectMode_ = value;
    rubberActive_ = false;
    if (!rubberBand_.isNull()) {
        rubberBand_ = {};
        emit rubberBandChanged();
    }
    setCursor(value ? Qt::CrossCursor : (panMode_ ? Qt::OpenHandCursor : Qt::ArrowCursor));
    emit zoomRectModeChanged();
}

void GpuCanvas::rememberView() {
    if (fitPending_) {
        return; // the initial automatic fit has no meaningful "previous" view
    }
    viewHistory_.push_back({camera_.scale(), camera_.offset()});
    if (viewHistory_.size() > 32) {
        viewHistory_.erase(viewHistory_.begin());
    }
}

void GpuCanvas::restoreView(double scale, ScenePoint offset) {
    camera_.zoomAt(scale / camera_.scale(), {0, 0});
    const auto now = camera_.offset();
    camera_.pan(offset.x - now.x, offset.y - now.y);
    viewUpdated();
}

void GpuCanvas::zoomToSceneRect(double x0, double y0, double x1, double y1) {
    if (width() <= 0 || height() <= 0) {
        return;
    }
    rememberView();
    camera_.setViewport(width(), height());
    camera_.fit({std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)}, 24.0);
    fitPending_ = false;
    viewUpdated();
}

bool GpuCanvas::zoomToSelection() {
    if (!scene_) {
        return false;
    }
    std::optional<stitch_render::RectMm> box;
    for (const auto& target : selection_) {
        if (target.kind != TargetKind::EmbroideryObject) {
            continue;
        }
        if (const auto b = scene_->boundsOf(target.id)) {
            box = box ? stitch_render::RectMm{std::min(box->x0, b->x0), std::min(box->y0, b->y0),
                                              std::max(box->x1, b->x1), std::max(box->y1, b->y1)}
                      : *b;
        }
    }
    if (!box) {
        return false;
    }
    zoomToSceneRect(box->x0, box->y0, box->x1, box->y1);
    return true;
}

void GpuCanvas::zoomToScale(double pxPerMm) {
    if (!std::isfinite(pxPerMm) || pxPerMm <= 0) {
        return;
    }
    rememberView();
    camera_.zoomAt(pxPerMm / camera_.scale(), {width() / 2.0, height() / 2.0});
    fitPending_ = false;
    viewUpdated();
}

bool GpuCanvas::zoomPrevious() {
    if (viewHistory_.empty()) {
        return false;
    }
    const auto saved = viewHistory_.back();
    viewHistory_.pop_back();
    restoreView(saved.scale, saved.offset);
    return true;
}

void GpuCanvas::centerOnScene(double x, double y) {
    const auto p = camera_.worldToScreen({x, y});
    panBy(width() / 2.0 - p.x, height() / 2.0 - p.y);
}

QVariantList GpuCanvas::rulerTicks(bool horizontal) const {
    QVariantList ticks;
    const double length = horizontal ? width() : height();
    if (length <= 0 || camera_.scale() <= 0) {
        return ticks;
    }
    // 1-2-5 step giving labels at least ~60 px apart.
    const double wanted = 60.0 / camera_.scale();
    const double decade = std::pow(10.0, std::floor(std::log10(wanted)));
    double step = decade * 10.0;
    for (const double m : {1.0, 2.0, 5.0, 10.0}) {
        if (decade * m >= wanted) {
            step = decade * m;
            break;
        }
    }
    const auto lo = camera_.screenToWorld({0, 0});
    const auto hi = camera_.screenToWorld({width(), height()});
    const double from = horizontal ? lo.x : lo.y;
    const double to = horizontal ? hi.x : hi.y;
    for (double v = std::ceil(from / step) * step; v <= to && ticks.size() < 60; v += step) {
        const auto s = camera_.worldToScreen({horizontal ? v : 0, horizontal ? 0 : v});
        // Labels in model mm: X right, Y UP (scene Y is down).
        const double label = horizontal ? v : -v;
        ticks.push_back(QVariantMap{{QStringLiteral("pos"), horizontal ? s.x : s.y},
                                    {QStringLiteral("label"), QString::number(label, 'g', 6)}});
    }
    return ticks;
}

void GpuCanvas::fitDesign() {
    if (!scene_ || width() <= 0 || height() <= 0) {
        return;
    }
    rememberView();
    camera_.setViewport(width(), height());
    camera_.fit(scene_->bounds);
    fitPending_ = false;
    viewUpdated();
}

void GpuCanvas::panBy(double dx, double dy) {
    camera_.pan(dx, dy);
    viewUpdated();
}

void GpuCanvas::zoomAt(double factor, double x, double y) {
    camera_.zoomAt(factor, {x, y});
    viewUpdated();
}

void GpuCanvas::viewUpdated() {
    update();
    emit viewChanged();
}

void GpuCanvas::geometryChange(const QRectF& next, const QRectF& previous) {
    QQuickItem::geometryChange(next, previous);
    camera_.setViewport(next.width(), next.height());
    if (fitPending_ && scene_ && next.width() > 0 && next.height() > 0) {
        fitDesign();
    } else {
        viewUpdated();
    }
}

QSGNode* GpuCanvas::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    if (!window()) {
        delete oldNode;
        return nullptr;
    }
    const auto api = window()->rendererInterface()->graphicsApi();
    const auto& backend =
        api == QSGRendererInterface::Software ? softwareBackend() : sceneGraphBackend();
    CanvasRenderState state{scene_,
                            camera_,
                            selection_,
                            selectionRevision_,
                            QSizeF(width(), height()),
                            window()->effectiveDevicePixelRatio(),
                            overlays_,
                            overlayRevision_};
    const auto result = backend.synchronize(oldNode, state, *window());
    // Render thread must never write QObject properties or read the live document. Queue
    // diagnostics.
    const auto name = apiName(api);
    if (result.geometryCreated || name != backendName_) {
        QMetaObject::invokeMethod(
            this,
            [this, name, created = result.geometryCreated, ms = result.resourceMs] {
                backendName_ = name;
                if (created) {
                    ++geometryBuildCount_;
                    resourceCreationMs_ = ms;
                }
                emit diagnosticsChanged();
            },
            Qt::QueuedConnection);
    }
    return result.node;
}

void GpuCanvas::wheelEvent(QWheelEvent* event) {
    forceActiveFocus();
    const double steps = event->angleDelta().y() != 0 ? event->angleDelta().y() / 120.0
                                                      : event->pixelDelta().y() / 40.0;
    zoomAt(std::pow(1.18, steps), event->position().x(), event->position().y());
    event->accept();
}

void GpuCanvas::mousePressEvent(QMouseEvent* event) {
    forceActiveFocus();
    lastMouse_ = event->position();
    dragging_ = event->button() == Qt::MiddleButton || panMode_ || spaceDown_;
    if (zoomRectMode_ && !dragging_ && event->button() == Qt::LeftButton) {
        rubberActive_ = true;
        rubberBand_ = QRectF(lastMouse_, QSizeF(0, 0));
        emit rubberBandChanged();
    } else if (dragging_) {
        setCursor(Qt::ClosedHandCursor);
    } else if (controller_ && scene_ && event->button() == Qt::LeftButton) {
        const auto p = camera_.screenToWorld({lastMouse_.x(), lastMouse_.y()});
        if (const auto id = scene_->pick(p.x, p.y, 5.0 / camera_.scale())) {
            controller_->selectObject(QString::number(*id),
                                      event->modifiers().testFlag(Qt::ControlModifier));
        } else if (!event->modifiers().testFlag(Qt::ControlModifier)) {
            controller_->clearSelection();
        }
    }
    event->accept();
}

void GpuCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (rubberActive_) {
        rubberBand_ = QRectF(lastMouse_, event->position()).normalized();
        emit rubberBandChanged();
    } else if (dragging_) {
        const auto delta = event->position() - lastMouse_;
        panBy(delta.x(), delta.y());
        lastMouse_ = event->position();
    }
    event->accept();
}

void GpuCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (rubberActive_) {
        rubberActive_ = false;
        const QRectF band = rubberBand_;
        rubberBand_ = {};
        emit rubberBandChanged();
        if (band.width() >= 4 && band.height() >= 4) {
            const auto a = camera_.screenToWorld({band.left(), band.top()});
            const auto b = camera_.screenToWorld({band.right(), band.bottom()});
            zoomToSceneRect(a.x, a.y, b.x, b.y);
        }
        setZoomRectMode(false); // one-shot, like the V1 tool
    }
    dragging_ = false;
    setCursor(panMode_ || spaceDown_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
    event->accept();
}

void GpuCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spaceDown_ = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
    } else if (event->key() == Qt::Key_F) {
        fitDesign();
        event->accept();
    } else if (event->key() == Qt::Key_Escape && controller_) {
        controller_->clearSelection();
        event->accept();
    } else {
        QQuickItem::keyPressEvent(event);
    }
}

void GpuCanvas::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spaceDown_ = false;
        setCursor(panMode_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
    } else {
        QQuickItem::keyReleaseEvent(event);
    }
}

void GpuCanvas::focusOutEvent(QFocusEvent* event) {
    spaceDown_ = false;
    dragging_ = false;
    setCursor(panMode_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
    QQuickItem::focusOutEvent(event);
}

} // namespace openstitch::desktop_v2::studio
