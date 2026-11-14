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
#include <cstdint>
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
        connect(controller_, &StudioController::documentReplaced, this, [this] {
            fitPending_ = true;
            viewHistory_.clear();
        });
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

QRectF GpuCanvas::hoverRect() const {
    if (!scene_ || !hoveredObjectId_ || !layers_.stitches) {
        return {};
    }
    // Selected objects already have their own outline and handles.
    if (std::ranges::find(selection_, Target{TargetKind::EmbroideryObject, *hoveredObjectId_}) !=
        selection_.end()) {
        return {};
    }
    const auto bounds = scene_->boundsOf(*hoveredObjectId_);
    if (!bounds) {
        return {};
    }
    const auto a = camera_.worldToScreen({bounds->x0, bounds->y0});
    const auto b = camera_.worldToScreen({bounds->x1, bounds->y1});
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
    const bool digitizeBusy = digitize_ && digitize_->cancel();
    if (digitizeBusy && gesture_ == Gesture::Digitize) {
        gesture_ = Gesture::None;
        gestureActive_ = false;
    }
    const bool toolBusy = (tools_ && tools_->cancel()) || digitizeBusy;
    if (toolBusy) {
        gesture_ = Gesture::None;
        gestureActive_ = false;
        emit overlayChanged();
    }
    const bool busy =
        gesture_ == Gesture::Move || gesture_ == Gesture::Handle || gesture_ == Gesture::Marquee;
    if (busy) {
        resetGesture();
    }
    return busy || toolBusy;
}

QString GpuCanvas::gestureInfo() const {
    if (digitize_ && !digitize_->info().isEmpty()) {
        return digitize_->info();
    }
    return tools_ && !tools_->info().isEmpty() ? tools_->info() : gestureInfo_;
}

void GpuCanvas::setDigitize(DigitizeGestures* digitize) {
    if (digitize_ == digitize) {
        return;
    }
    if (digitize_) {
        disconnect(digitize_, nullptr, this, nullptr);
    }
    digitize_ = digitize;
    if (digitize_) {
        connect(digitize_, &DigitizeGestures::overlayChanged, this, &GpuCanvas::overlayChanged);
        connect(digitize_, &DigitizeGestures::modeChanged, this, [this] {
            setCursor(digitize_ && digitize_->capturesMouse() ? Qt::CrossCursor : Qt::ArrowCursor);
            emit overlayChanged();
        });
        connect(digitize_, &QObject::destroyed, this, [this] {
            digitize_ = nullptr;
            emit digitizeChanged();
        });
    }
    emit digitizeChanged();
    emit overlayChanged();
}

QVariantList GpuCanvas::handlesOverlay() const {
    static const char* const kKinds[] = {"line",   "closed", "dots", "selected",
                                         "handle", "active", "ghost"};
    constexpr int kMaxDots = 8000;
    QVariantList out;
    if (!digitize_ || !digitize_->capturesMouse()) {
        return out;
    }
    // Fenêtre visible en mm scène : les points hors champ ne sont jamais convertis ni dessinés.
    const auto a = camera_.screenToWorld({0.0, 0.0});
    const auto b = camera_.screenToWorld({width(), height()});
    const double x0 = std::min(a.x, b.x) - 1.0, x1 = std::max(a.x, b.x) + 1.0;
    const double y0 = std::min(a.y, b.y) - 1.0, y1 = std::max(a.y, b.y) + 1.0;
    const auto append = [&](const HandleItem& item) {
        const bool cloud =
            item.kind == HandleItem::Kind::Dots || item.kind == HandleItem::Kind::Selected;
        QVariantList pts;
        int kept = 0;
        for (const auto& p : item.points) {
            if (cloud && (p.x < x0 || p.x > x1 || p.y < y0 || p.y > y1)) {
                continue;
            }
            if (cloud && ++kept > kMaxDots) {
                break;
            }
            const auto s = camera_.worldToScreen(p);
            pts << s.x << s.y;
        }
        if (!pts.isEmpty()) {
            out.push_back(QVariantMap{
                {QStringLiteral("kind"), QString::fromLatin1(kKinds[static_cast<int>(item.kind)])},
                {QStringLiteral("pts"), pts}});
        }
    };
    for (const auto& item : digitize_->staticItems()) {
        append(item);
    }
    for (const auto& item : digitize_->dynamicItems()) {
        append(item);
    }
    return out;
}

void GpuCanvas::setTools(ToolController* tools) {
    if (tools_ == tools) {
        return;
    }
    if (tools_) {
        disconnect(tools_, nullptr, this, nullptr);
    }
    tools_ = tools;
    if (tools_) {
        connect(tools_, &ToolController::overlayChanged, this, &GpuCanvas::overlayChanged);
        connect(tools_, &ToolController::activeToolChanged, this, [this] {
            setCursor(tools_ && tools_->capturesMouse() ? Qt::CrossCursor : Qt::ArrowCursor);
            emit overlayChanged();
        });
        connect(tools_, &QObject::destroyed, this, [this] {
            tools_ = nullptr;
            emit toolsChanged();
        });
    }
    emit toolsChanged();
    emit overlayChanged();
}

QVariantList GpuCanvas::toolOverlay() const {
    static const char* const kKinds[] = {"line", "closed", "point", "selected", "tangent", "ghost"};
    QVariantList out;
    if (!tools_) {
        return out;
    }
    // Rayon d'un nœud proportionnel à la forme éditée (~1,5 % de sa diagonale à l'écran), borné.
    const double spanPx = tools_->nodeSpanMm() * camera_.scale();
    const double markerPx = std::clamp(spanPx * 0.015, 1.5, 3.5);
    // QML Canvas node markers are visible only inside the viewport. Large imported paths can
    // carry tens of thousands of nodes: in addition to culling offscreen markers, keep one
    // ordinary marker per screen-space cell. This is display-only LOD: ToolController retains
    // every NodeHandle for hit-testing and editing, and selected nodes always bypass the LOD.
    // Do not cull lines: an edge may cross the viewport with both endpoints outside.
    constexpr double marginPx = 8.0;
    constexpr double minCellPx = 8.0;
    constexpr double markerBudget = 3'000.0;
    constexpr double minPolylineStepPx = 2.0;
    const double viewportArea = std::max(1.0, width() * height());
    const double cellPx = std::max(minCellPx, std::sqrt(viewportArea / markerBudget));
    const int columns =
        std::max(1, static_cast<int>(std::ceil((width() + 2.0 * marginPx) / cellPx)));
    const int rows = std::max(1, static_cast<int>(std::ceil((height() + 2.0 * marginPx) / cellPx)));
    std::vector<std::uint8_t> occupied(
        static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows), 0);
    for (const auto& item : tools_->overlay()) {
        const bool isMarker = item.kind == ToolOverlayItem::Kind::Point ||
                              item.kind == ToolOverlayItem::Kind::Selected;
        const bool keepEveryMarker = item.kind == ToolOverlayItem::Kind::Selected;
        const bool simplifyPolyline = !isMarker && item.points.size() > 2;
        QVariantList pts;
        double lastLineX = 0.0;
        double lastLineY = 0.0;
        bool hasLinePoint = false;
        for (std::size_t index = 0; index < item.points.size(); ++index) {
            const auto& p = item.points[index];
            const auto s = camera_.worldToScreen(p);
            if (isMarker && (s.x < -marginPx || s.y < -marginPx || s.x > width() + marginPx ||
                             s.y > height() + marginPx)) {
                continue;
            }
            if (isMarker && !keepEveryMarker) {
                const int column = std::clamp(
                    static_cast<int>(std::floor((s.x + marginPx) / cellPx)), 0, columns - 1);
                const int row = std::clamp(static_cast<int>(std::floor((s.y + marginPx) / cellPx)),
                                           0, rows - 1);
                auto& used =
                    occupied[static_cast<std::size_t>(row) * static_cast<std::size_t>(columns) +
                             static_cast<std::size_t>(column)];
                if (used != 0) {
                    continue;
                }
                used = 1;
            }
            // The vector image below already carries the full geometry. The selection outline is
            // only a screen-space cue: avoid sending sub-pixel vertices to the QML Canvas while
            // retaining the first and last point so open/crossing segments stay intact.
            if (simplifyPolyline && hasLinePoint && index + 1 < item.points.size() &&
                std::hypot(s.x - lastLineX, s.y - lastLineY) < minPolylineStepPx) {
                continue;
            }
            pts << s.x << s.y;
            if (simplifyPolyline) {
                lastLineX = s.x;
                lastLineY = s.y;
                hasLinePoint = true;
            }
        }
        if (pts.isEmpty()) {
            continue;
        }
        QVariantMap entry{
            {QStringLiteral("kind"), QString::fromLatin1(kKinds[static_cast<int>(item.kind)])},
            {QStringLiteral("pts"), pts}};
        if (item.kind == ToolOverlayItem::Kind::Point ||
            item.kind == ToolOverlayItem::Kind::Selected) {
            entry.insert(QStringLiteral("r"), markerPx);
        }
        out.push_back(entry);
    }
    return out;
}

void GpuCanvas::setScene(std::shared_ptr<const StudioSceneSnapshot> scene) {
    if (scene_ == scene) {
        return;
    }
    scene_ = std::move(scene);
    hoveredObjectId_.reset();
    emit overlayChanged();
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
    if (hoveredObjectId_) {
        emit overlayChanged();
    }
    update();
}

void GpuCanvas::setPanMode(bool value) {
    if (value == panMode_) {
        return;
    }
    panMode_ = value;
    if (value && hoveredObjectId_) {
        hoveredObjectId_.reset();
        emit overlayChanged();
    }
    setCursor(value ? Qt::OpenHandCursor : Qt::ArrowCursor);
    emit panModeChanged();
}

void GpuCanvas::setOverlays(std::shared_ptr<const CanvasOverlays> overlays) {
    // Les overlays sont des snapshots immuables : une même instance ne peut pas changer.
    if (overlays_ == overlays) {
        return;
    }
    overlays_ = std::move(overlays);
    ++overlayRevision_;
    update();
}

void GpuCanvas::setSourceImage(SourceImageView view) {
    if (view == sourceImage_) {
        return;
    }
    sourceImage_ = std::move(view);
    update();
}

void GpuCanvas::setVectorLayer(std::shared_ptr<const OverlayImage> image) {
    if (vectorImage_ == image) {
        return; // pas de texture à republier pour un snapshot identique
    }
    vectorImage_ = std::move(image);
    update(); // le backend suit séparément l'identité de l'image vectorielle
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

QVariantMap GpuCanvas::layers() const {
    return {{QStringLiteral("sourceImage"), layers_.sourceImage},
            {QStringLiteral("regions"), layers_.regions},
            {QStringLiteral("vectors"), layers_.vectors},
            {QStringLiteral("stitches"), layers_.stitches},
            {QStringLiteral("selectionOverlays"), layers_.selectionOverlays}};
}

void GpuCanvas::setRegionLayer(std::shared_ptr<const RegionLayerData> data) {
    if (regionLayer_ == data) {
        return;
    }
    regionLayer_ = std::move(data);
    update();
}

void GpuCanvas::setLayers(const QVariantMap& layers) {
    CanvasLayers next = layers_;
    const auto read = [&](const char* key, bool& flag) {
        const auto it = layers.constFind(QString::fromLatin1(key));
        if (it != layers.constEnd()) {
            flag = it->toBool();
        }
    };
    read("sourceImage", next.sourceImage);
    read("regions", next.regions);
    read("vectors", next.vectors);
    read("stitches", next.stitches);
    read("selectionOverlays", next.selectionOverlays);
    if (next == layers_) {
        return;
    }
    layers_ = next;
    update();
    emit layersChanged();
    emit overlayChanged();
}

QVariantMap GpuCanvas::viewState() const {
    if (viewPending_) {
        return {{QStringLiteral("centerX"), pendingX_},
                {QStringLiteral("centerY"), pendingY_},
                {QStringLiteral("zoom"), pendingZoom_}};
    }
    const auto centre = camera_.screenToWorld({width() / 2.0, height() / 2.0});
    return {{QStringLiteral("centerX"), centre.x},
            {QStringLiteral("centerY"), centre.y},
            {QStringLiteral("zoom"), camera_.scale()}};
}

void GpuCanvas::restoreViewState(const QVariantMap& state) {
    const double zoom = state.value(QStringLiteral("zoom")).toDouble();
    const double x = state.value(QStringLiteral("centerX")).toDouble();
    const double y = state.value(QStringLiteral("centerY")).toDouble();
    if (!std::isfinite(zoom) || zoom <= 0 || !std::isfinite(x) || !std::isfinite(y)) {
        return;
    }
    pendingZoom_ = zoom;
    pendingX_ = x;
    pendingY_ = y;
    viewPending_ = true;
    fitPending_ = false;
    applyPendingView();
}

void GpuCanvas::applyPendingView() {
    if (!viewPending_ || width() <= 0 || height() <= 0) {
        return;
    }
    viewPending_ = false;
    camera_.setViewport(width(), height());
    camera_.zoomAt(pendingZoom_ / camera_.scale(), {width() / 2.0, height() / 2.0});
    const auto p = camera_.worldToScreen({pendingX_, pendingY_});
    camera_.pan(width() / 2.0 - p.x, height() / 2.0 - p.y);
    viewUpdated();
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
    emit overlayChanged();
}

void GpuCanvas::geometryChange(const QRectF& next, const QRectF& previous) {
    QQuickItem::geometryChange(next, previous);
    camera_.setViewport(next.width(), next.height());
    if (viewPending_) {
        applyPendingView();
    } else if (fitPending_ && scene_ && next.width() > 0 && next.height() > 0) {
        fitDesign();
    } else {
        viewUpdated();
    }
}

bool GpuCanvas::rhiBackend() const {
    if (window() == nullptr || window()->rendererInterface() == nullptr) {
        return false;
    }
    const auto api = window()->rendererInterface()->graphicsApi();
    return api != QSGRendererInterface::Software && api != QSGRendererInterface::Unknown &&
           api != QSGRendererInterface::Null;
}

QSGNode* GpuCanvas::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    if (!window()) {
        delete oldNode;
        return nullptr;
    }
    const auto api = window()->rendererInterface()->graphicsApi();
    const auto& backend =
        api == QSGRendererInterface::Software ? softwareBackend() : sceneGraphBackend();
    CanvasRenderState state{layers_.stitches ? scene_ : nullptr,
                            camera_,
                            layers_.selectionOverlays ? selection_ : std::vector<Target>{},
                            selectionRevision_,
                            QSizeF(width(), height()),
                            window()->effectiveDevicePixelRatio(),
                            overlays_,
                            overlayRevision_,
                            layers_,
                            layers_.sourceImage ? sourceImage_ : SourceImageView{},
                            regionLayer_,
                            layers_.vectors ? vectorImage_ : nullptr};
    const auto result = backend.synchronize(oldNode, state, *window());
    // Render thread must never write QObject properties or read the live document. Queue
    // diagnostics.
    const auto name = apiName(api);
    if (result.geometryCreated || result.realisticBuilt || name != backendName_ ||
        result.realisticActive != realisticActive_) {
        QMetaObject::invokeMethod(
            this,
            [this, name, created = result.geometryCreated, ms = result.resourceMs,
             realBuilt = result.realisticBuilt, realMs = result.realisticMs,
             realActive = result.realisticActive] {
                backendName_ = name;
                if (created) {
                    ++geometryBuildCount_;
                    resourceCreationMs_ = ms;
                }
                if (realBuilt) {
                    ++realisticBuildCount_;
                    realisticBuildMs_ = realMs;
                }
                realisticActive_ = realActive;
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
    if (zoomRectMode_ && event->button() == Qt::LeftButton) {
        gesture_ = Gesture::ZoomRect; // one-shot rubber band, see zoomRectangle()
        rubberBand_ = QRectF(pressPos_, QSizeF(0, 0));
        emit rubberBandChanged();
        event->accept();
        return;
    }
    const bool panning = event->button() == Qt::MiddleButton || panMode_ || spaceDown_;
    if (panning) {
        gesture_ = Gesture::Pan;
        setCursor(Qt::ClosedHandCursor);
    } else if (digitize_ && digitize_->capturesMouse() && event->button() == Qt::LeftButton) {
        const auto p = camera_.screenToWorld({lastMouse_.x(), lastMouse_.y()});
        if (digitize_->press(p.x, p.y, camera_.scale(), event->modifiers())) {
            gesture_ = Gesture::Digitize;
            gestureActive_ = true;
        }
    } else if (tools_ && tools_->capturesMouse() && event->button() == Qt::LeftButton) {
        const auto p = camera_.screenToWorld({lastMouse_.x(), lastMouse_.y()});
        if (tools_->press(p.x, p.y, camera_.scale(), event->modifiers())) {
            gesture_ = Gesture::Tool;
            gestureActive_ = true;
        }
    } else if (tools_ && event->button() == Qt::LeftButton && !tools_->capturesMouse() &&
               tools_->press(camera_.screenToWorld({lastMouse_.x(), lastMouse_.y()}).x,
                             camera_.screenToWorld({lastMouse_.x(), lastMouse_.y()}).y,
                             camera_.scale(), event->modifiers())) {
        // Sélection : un clic sur un nœud de la forme sélectionnée édite le contour ; sinon
        // ToolController::press rend false et la sélection classique ci-dessous s'applique.
        gesture_ = Gesture::Tool;
        gestureActive_ = true;
    } else if (vectorPicker_ && layers_.vectors && !layers_.stitches &&
               event->button() == Qt::LeftButton) {
        // Espace Vectorisation : le clic choisit une FORME (couche vectors), pas un objet de
        // points.
        const auto p = camera_.screenToWorld({lastMouse_.x(), lastMouse_.y()});
        vectorPicker_(p.x, p.y, 5.0 / camera_.scale(), ctrl);
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
    } else if (gesture_ == Gesture::ZoomRect) {
        rubberBand_ = QRectF(pressPos_, pos).normalized();
        emit rubberBandChanged();
    } else if (gesture_ == Gesture::Tool) {
        if (tools_) {
            const auto p = camera_.screenToWorld({pos.x(), pos.y()});
            tools_->move(p.x, p.y, event->modifiers());
        }
    } else if (gesture_ == Gesture::Digitize) {
        if (digitize_) {
            const auto p = camera_.screenToWorld({pos.x(), pos.y()});
            digitize_->move(p.x, p.y, event->modifiers());
        }
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
    if (gesture_ == Gesture::ZoomRect) {
        const QRectF band = rubberBand_;
        gesture_ = Gesture::None;
        rubberBand_ = {};
        emit rubberBandChanged();
        if (band.width() >= 4 && band.height() >= 4) {
            const auto a = camera_.screenToWorld({band.left(), band.top()});
            const auto b = camera_.screenToWorld({band.right(), band.bottom()});
            zoomToSceneRect(a.x, a.y, b.x, b.y);
        }
        setZoomRectMode(false);
        event->accept();
        return;
    }
    if (gesture_ == Gesture::Tool) {
        gesture_ = Gesture::None;
        gestureActive_ = false;
        if (tools_) {
            const auto p = camera_.screenToWorld({event->position().x(), event->position().y()});
            tools_->release(p.x, p.y, event->modifiers());
        }
        emit overlayChanged();
        event->accept();
        return;
    }
    if (gesture_ == Gesture::Digitize) {
        gesture_ = Gesture::None;
        gestureActive_ = false;
        if (digitize_) {
            const auto p = camera_.screenToWorld({event->position().x(), event->position().y()});
            digitize_->release(p.x, p.y, event->modifiers());
        }
        emit overlayChanged();
        event->accept();
        return;
    }
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

void GpuCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (tools_ && tools_->capturesMouse() && event->button() == Qt::LeftButton) {
        const auto p = camera_.screenToWorld({event->position().x(), event->position().y()});
        tools_->doubleClick(p.x, p.y, camera_.scale());
        // Les outils qui ne se terminent pas au double-clic reçoivent aussi l'appui : deux gestes
        // rapprochés au même endroit ne doivent pas perdre le second.
        const auto tool = tools_->activeTool();
        if (tool != QLatin1String("draw.polygon") && tool != QLatin1String("draw.bezier")) {
            mousePressEvent(event);
            return;
        }
        event->accept();
        return;
    }
    QQuickItem::mouseDoubleClickEvent(event);
}

void GpuCanvas::hoverMoveEvent(QHoverEvent* event) {
    // Preselection is UI-only; the scene's spatial index answers the hit test.
    // Emit only when the hovered object changes, never for every pointer pixel.
    const auto setHover = [this](std::optional<std::uint64_t> id) {
        if (hoveredObjectId_ != id) {
            hoveredObjectId_ = id;
            emit overlayChanged();
        }
    };
    if (gesture_ != Gesture::None || panMode_ || spaceDown_) {
        setHover(std::nullopt);
        return;
    }
    if (digitize_ && digitize_->capturesMouse()) {
        setHover(std::nullopt);
        setCursor(Qt::CrossCursor);
        return;
    }
    if (tools_ && tools_->capturesMouse()) {
        setHover(std::nullopt);
        setCursor(Qt::CrossCursor);
        if (tools_->busy()) {
            const auto p = camera_.screenToWorld({event->position().x(), event->position().y()});
            tools_->move(p.x, p.y, event->modifiers());
        }
        return;
    }
    static constexpr Qt::CursorShape shapes[] = {
        Qt::SizeFDiagCursor, Qt::SizeVerCursor,   Qt::SizeBDiagCursor,
        Qt::SizeHorCursor,   Qt::SizeFDiagCursor, Qt::SizeVerCursor,
        Qt::SizeBDiagCursor, Qt::SizeHorCursor,   Qt::PointingHandCursor};
    const int handle = editor_ ? handleAt(event->position()) : -1;
    if (handle >= 0) {
        setHover(std::nullopt);
        setCursor(shapes[handle]);
        return;
    }
    std::optional<std::uint64_t> hovered;
    if (scene_ && layers_.stitches && !zoomRectMode_) {
        const auto p = camera_.screenToWorld({event->position().x(), event->position().y()});
        hovered = scene_->pick(p.x, p.y, 5.0 / camera_.scale());
    }
    setHover(hovered);
    setCursor(hovered ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void GpuCanvas::hoverLeaveEvent(QHoverEvent* event) {
    if (hoveredObjectId_) {
        hoveredObjectId_.reset();
        emit overlayChanged();
    }
    QQuickItem::hoverLeaveEvent(event);
}

void GpuCanvas::keyPressEvent(QKeyEvent* event) {
    if (digitize_ && event->key() != Qt::Key_Space && digitize_->keyPress(event->key())) {
        event->accept();
        return;
    }
    if (tools_ && event->key() != Qt::Key_Space && tools_->keyPress(event->key())) {
        event->accept();
        return;
    }
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

#include "workspace_controller.hpp"
#include <QCoreApplication>

// Enregistre WorkspaceController au démarrage de QCoreApplication (hôte et tests de la coque).
static void registerStudioWorkspaceTypes() {
    openstitch::desktop_v2::studio::registerWorkspaceTypes();
}
Q_COREAPP_STARTUP_FUNCTION(registerStudioWorkspaceTypes)
