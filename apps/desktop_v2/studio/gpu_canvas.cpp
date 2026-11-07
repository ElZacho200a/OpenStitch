// SPDX-License-Identifier: Apache-2.0
#include "gpu_canvas.hpp"

#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <optional>

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
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
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
        connect(controller_, &StudioController::documentReplaced, this,
                [this] { fitPending_ = true; });
        connect(controller_, &StudioController::sceneChanged, this,
                [this] { setScene(controller_->scene()); });
        connect(controller_, &StudioController::selectionChanged, this, [this] {
            setSelection(controller_->session().selection().targets());
            emit overlayChanged();
        });
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

void GpuCanvas::setEditor(EditController* editor) {
    if (editor_ == editor) {
        return;
    }
    if (editor_) {
        disconnect(editor_, nullptr, this, nullptr);
    }
    editor_ = editor;
    if (editor_) {
        connect(editor_, &EditController::treeChanged, this, &GpuCanvas::overlayChanged);
        connect(editor_, &QObject::destroyed, this, [this] {
            editor_ = nullptr;
            emit editorChanged();
        });
    }
    emit editorChanged();
    emit overlayChanged();
}

QRectF GpuCanvas::selectionRect() const {
    const auto box = editor_ ? editor_->selectionBounds() : std::nullopt;
    if (!box) {
        return {};
    }
    const auto a = camera_.worldToScreen({box->x0, box->y0});
    const auto b = camera_.worldToScreen({box->x1, box->y1});
    return QRectF(QPointF(a.x, a.y), QPointF(b.x, b.y)).normalized();
}

QPointF GpuCanvas::handlePoint(int handle, const QRectF& box) const {
    const double cx = box.center().x();
    const double cy = box.center().y();
    switch (handle) {
    case 0:
        return box.topLeft();
    case 1:
        return {cx, box.top()};
    case 2:
        return box.topRight();
    case 3:
        return {box.right(), cy};
    case 4:
        return box.bottomRight();
    case 5:
        return {cx, box.bottom()};
    case 6:
        return box.bottomLeft();
    case 7:
        return {box.left(), cy};
    default:
        return {cx, box.top() - 26.0};
    }
}

int GpuCanvas::handleAt(const QPointF& p) const {
    if (!editor_ || !editor_->selectionBounds()) {
        return -1;
    }
    const auto box = selectionRect();
    for (int handle = 8; handle >= 0; --handle) { // la rotation d'abord : elle est au-dessus
        const auto h = handlePoint(handle, box);
        if (std::abs(p.x() - h.x()) <= 7.0 && std::abs(p.y() - h.y()) <= 7.0) {
            return handle;
        }
    }
    return -1;
}

void GpuCanvas::resetGesture() {
    const bool changed = gesture_ != Gesture::None || !ghost_.isNull() || !marquee_.isNull();
    gesture_ = Gesture::None;
    gestureActive_ = false;
    handle_ = -1;
    marquee_ = {};
    ghost_ = {};
    guides_.clear();
    gestureInfo_.clear();
    handleFactor_ = 1.0;
    handleAngle_ = 0.0;
    if (editor_) {
        editor_->cancelMove();
    }
    if (changed) {
        emit overlayChanged();
    }
}

bool GpuCanvas::cancelGesture() {
    const bool busy =
        gesture_ == Gesture::Move || gesture_ == Gesture::Handle || gesture_ == Gesture::Marquee;
    if (busy) {
        resetGesture();
    }
    return busy;
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

void GpuCanvas::fitDesign() {
    if (!scene_ || width() <= 0 || height() <= 0) {
        return;
    }
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
    emit overlayChanged();
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
    pressPos_ = event->position();
    resetGesture();
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    if (event->button() == Qt::RightButton) {
        std::optional<std::uint64_t> id;
        if (controller_ && scene_) {
            const auto p = camera_.screenToWorld({lastMouse_.x(), lastMouse_.y()});
            id = scene_->pick(p.x, p.y, 5.0 / camera_.scale());
            if (id && std::ranges::find(selection_, Target{TargetKind::EmbroideryObject, *id}) ==
                          selection_.end()) {
                controller_->selectObject(QString::number(*id), false);
            } else if (!id && !selectionRect().contains(lastMouse_)) {
                controller_->clearSelection();
            }
        }
        emit contextMenuRequested(lastMouse_.x(), lastMouse_.y(),
                                  id ? QString::number(*id) : QString());
        event->accept();
        return;
    }
    const bool panning = event->button() == Qt::MiddleButton || panMode_ || spaceDown_;
    if (panning) {
        gesture_ = Gesture::Pan;
        setCursor(Qt::ClosedHandCursor);
    } else if (controller_ && scene_ && event->button() == Qt::LeftButton) {
        const auto p = camera_.screenToWorld({lastMouse_.x(), lastMouse_.y()});
        const auto hit = scene_->pick(p.x, p.y, 5.0 / camera_.scale());
        const bool pipette = editor_ && editor_->eyedropper();
        const int handle = editor_ && !pipette ? handleAt(lastMouse_) : -1;
        if (pipette) {
            if (hit) {
                editor_->applyColorFrom(QString::number(*hit));
            }
        } else if (handle >= 0) {
            gesture_ = Gesture::Handle;
            handle_ = handle;
            handleBox_ = selectionRect();
        } else if (hit) {
            controller_->selectObject(QString::number(*hit), ctrl);
            const Target target{TargetKind::EmbroideryObject, *hit};
            if (!ctrl && editor_ && std::ranges::find(selection_, target) != selection_.end() &&
                editor_->beginMove()) {
                gesture_ = Gesture::Move;
            }
        } else if (!ctrl && editor_ && selectionRect().contains(lastMouse_) &&
                   editor_->beginMove()) {
            gesture_ = Gesture::Move; // glisser dans la boîte, même entre deux fils
        } else {
            if (!ctrl) {
                controller_->clearSelection();
            }
            if (editor_) {
                gesture_ = Gesture::Marquee;
            }
        }
    }
    event->accept();
}

void GpuCanvas::mouseMoveEvent(QMouseEvent* event) {
    const auto pos = event->position();
    if (gesture_ == Gesture::Pan) {
        const auto delta = pos - lastMouse_;
        panBy(delta.x(), delta.y());
        lastMouse_ = pos;
    } else if (gesture_ != Gesture::None) {
        const auto travel = pos - pressPos_;
        if (!gestureActive_ && std::abs(travel.x()) + std::abs(travel.y()) < 3.0) {
            event->accept();
            return;
        }
        gestureActive_ = true;
        const double scale = camera_.scale();
        if (gesture_ == Gesture::Marquee) {
            marquee_ = QRectF(pressPos_, pos).normalized();
        } else if (gesture_ == Gesture::Move) {
            const auto preview =
                editor_->previewMove(travel.x() / scale, travel.y() / scale, 8.0 / scale,
                                     !event->modifiers().testFlag(Qt::AltModifier));
            moveDx_ = preview.value(QStringLiteral("dx")).toDouble();
            moveDy_ = preview.value(QStringLiteral("dy")).toDouble();
            ghost_ = selectionRect().translated(moveDx_ * scale, moveDy_ * scale);
            guides_.clear();
            for (const auto& entry : preview.value(QStringLiteral("guides")).toList()) {
                const auto g = entry.toMap();
                const auto position = g.value(QStringLiteral("position")).toDouble();
                const auto from = g.value(QStringLiteral("from")).toDouble();
                const auto to = g.value(QStringLiteral("to")).toDouble();
                const bool vertical = g.value(QStringLiteral("vertical")).toBool();
                const auto a = camera_.worldToScreen(vertical ? ScenePoint{position, from}
                                                              : ScenePoint{from, position});
                const auto b = camera_.worldToScreen(vertical ? ScenePoint{position, to}
                                                              : ScenePoint{to, position});
                guides_.push_back(QVariantMap{{QStringLiteral("x1"), a.x},
                                              {QStringLiteral("y1"), a.y},
                                              {QStringLiteral("x2"), b.x},
                                              {QStringLiteral("y2"), b.y}});
            }
            gestureInfo_ =
                QStringLiteral("%1 ; %2 mm").arg(moveDx_, 0, 'f', 1).arg(-moveDy_, 0, 'f', 1);
        } else if (gesture_ == Gesture::Handle) {
            const auto centre = handleBox_.center();
            if (handle_ == 8) {
                const double a0 =
                    std::atan2(pressPos_.y() - centre.y(), pressPos_.x() - centre.x());
                const double a1 = std::atan2(pos.y() - centre.y(), pos.x() - centre.x());
                double degrees = -(a1 - a0) * 180.0 / 3.14159265358979323846;
                while (degrees > 180.0) {
                    degrees -= 360.0;
                }
                while (degrees < -180.0) {
                    degrees += 360.0;
                }
                if (event->modifiers().testFlag(Qt::ShiftModifier)) {
                    degrees = std::round(degrees / 15.0) * 15.0;
                }
                handleAngle_ = degrees;
                ghost_ = handleBox_;
                gestureInfo_ = QStringLiteral("%1°").arg(degrees, 0, 'f', 0);
            } else {
                const auto grabbed = handlePoint(handle_, handleBox_);
                const auto anchor = handlePoint((handle_ + 4) % 8, handleBox_);
                const bool horizontal = handle_ != 1 && handle_ != 5;
                const bool vertical = handle_ != 3 && handle_ != 7;
                double f = 0.0;
                if (horizontal && std::abs(grabbed.x() - anchor.x()) > 1e-6) {
                    f = std::max(f, (pos.x() - anchor.x()) / (grabbed.x() - anchor.x()));
                }
                if (vertical && std::abs(grabbed.y() - anchor.y()) > 1e-6) {
                    f = std::max(f, (pos.y() - anchor.y()) / (grabbed.y() - anchor.y()));
                }
                handleFactor_ = std::clamp(f, 0.05, 20.0);
                ghost_ = QRectF(anchor + (handleBox_.topLeft() - anchor) * handleFactor_,
                                anchor + (handleBox_.bottomRight() - anchor) * handleFactor_)
                             .normalized();
                gestureInfo_ = QStringLiteral("× %1").arg(handleFactor_, 0, 'f', 2);
            }
        }
        emit overlayChanged();
    }
    event->accept();
}

void GpuCanvas::mouseReleaseEvent(QMouseEvent* event) {
    const auto gesture = gesture_;
    const bool active = gestureActive_;
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    const auto marquee = marquee_;
    const auto handle = handle_;
    const auto box = handleBox_;
    const auto factor = handleFactor_;
    const auto angle = handleAngle_;
    const auto dx = moveDx_;
    const auto dy = moveDy_;
    // Le geste est terminé avant de muter : la mutation relance toute la chaîne de signaux.
    gesture_ = Gesture::None;
    gestureActive_ = false;
    handle_ = -1;
    marquee_ = {};
    ghost_ = {};
    guides_.clear();
    gestureInfo_.clear();
    if (active && editor_) {
        if (gesture == Gesture::Marquee) {
            const auto a = camera_.screenToWorld({marquee.left(), marquee.top()});
            const auto b = camera_.screenToWorld({marquee.right(), marquee.bottom()});
            // Glisser de gauche à droite : fenêtre (objets entièrement dedans) ; de droite à
            // gauche : capture (tout objet touché), comme dans les logiciels de dessin.
            const bool crossing = event->position().x() < pressPos_.x();
            editor_->selectRect(a.x, a.y, b.x, b.y, crossing, ctrl);
        } else if (gesture == Gesture::Move) {
            editor_->commitMove(dx, dy);
        } else if (gesture == Gesture::Handle) {
            if (handle == 8) {
                editor_->rotate(angle);
            } else {
                const auto anchor = handlePoint((handle + 4) % 8, box);
                const auto world = camera_.screenToWorld({anchor.x(), anchor.y()});
                editor_->scale(factor, world.x, world.y);
            }
        }
    } else if (editor_) {
        editor_->cancelMove();
    }
    setCursor(panMode_ || spaceDown_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
    emit overlayChanged();
    event->accept();
}

void GpuCanvas::hoverMoveEvent(QHoverEvent* event) {
    if (gesture_ != Gesture::None || panMode_ || spaceDown_) {
        return;
    }
    static constexpr Qt::CursorShape shapes[] = {
        Qt::SizeFDiagCursor, Qt::SizeVerCursor,   Qt::SizeBDiagCursor,
        Qt::SizeHorCursor,   Qt::SizeFDiagCursor, Qt::SizeVerCursor,
        Qt::SizeBDiagCursor, Qt::SizeHorCursor,   Qt::PointingHandCursor};
    const int handle = editor_ ? handleAt(event->position()) : -1;
    setCursor(handle >= 0 ? shapes[handle] : Qt::ArrowCursor);
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
        if (gesture_ != Gesture::None) {
            resetGesture();
        } else {
            controller_->clearSelection();
        }
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
    resetGesture();
    setCursor(panMode_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
    QQuickItem::focusOutEvent(event);
}

} // namespace openstitch::desktop_v2::studio
