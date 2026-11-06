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
    if (controller_) {
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
    const bool first = !scene_;
    scene_ = std::move(scene);
    if (first && scene_) {
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

void GpuCanvas::fitDesign() {
    if (!scene_) {
        return;
    }
    camera_.setViewport(width(), height());
    camera_.fit(scene_->bounds);
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
    viewUpdated();
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
                            window()->effectiveDevicePixelRatio()};
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
    if (dragging_) {
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
    if (dragging_) {
        const auto delta = event->position() - lastMouse_;
        panBy(delta.x(), delta.y());
        lastMouse_ = event->position();
    }
    event->accept();
}

void GpuCanvas::mouseReleaseEvent(QMouseEvent* event) {
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
