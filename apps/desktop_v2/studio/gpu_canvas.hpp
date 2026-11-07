// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QPointF>
#include <QQuickItem>
#include <QRectF>
#include <QVariantList>
#include <memory>
#include <vector>

#include "canvas_renderer.hpp"
#include "studio_camera.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class GpuCanvas : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(
        StudioController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(bool panMode READ panMode WRITE setPanMode NOTIFY panModeChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(bool zoomRectMode READ zoomRectMode WRITE setZoomRectMode NOTIFY zoomRectModeChanged)
    Q_PROPERTY(QRectF rubberBand READ rubberBand NOTIFY rubberBandChanged)
    Q_PROPERTY(bool canZoomPrevious READ canZoomPrevious NOTIFY viewChanged)
    Q_PROPERTY(QString backendName READ backendName NOTIFY diagnosticsChanged)
    Q_PROPERTY(int geometryBuildCount READ geometryBuildCount NOTIFY diagnosticsChanged)
    Q_PROPERTY(double resourceCreationMs READ resourceCreationMs NOTIFY diagnosticsChanged)
public:
    explicit GpuCanvas(QQuickItem* parent = nullptr);
    [[nodiscard]] StudioController* controller() const { return controller_; }
    void setController(StudioController* controller);
    void setScene(std::shared_ptr<const StudioSceneSnapshot> scene);
    void setSelection(std::vector<Target> selection);
    [[nodiscard]] const std::shared_ptr<const StudioSceneSnapshot>& scene() const { return scene_; }
    [[nodiscard]] bool panMode() const { return panMode_; }
    void setPanMode(bool value);
    [[nodiscard]] double zoom() const { return camera_.scale(); }
    [[nodiscard]] bool zoomRectMode() const { return zoomRectMode_; }
    void setZoomRectMode(bool value);
    [[nodiscard]] QRectF rubberBand() const { return rubberBand_; }
    [[nodiscard]] bool canZoomPrevious() const { return !viewHistory_.empty(); }
    // Display-only layers (grid, guides, maps, markers, simulation cursor). One new immutable
    // instance per change; stitch geometry is never rebuilt for it.
    void setOverlays(std::shared_ptr<const CanvasOverlays> overlays);
    [[nodiscard]] const std::shared_ptr<const CanvasOverlays>& overlays() const {
        return overlays_;
    }
    [[nodiscard]] std::uint64_t overlayRevision() const { return overlayRevision_; }
    [[nodiscard]] QString backendName() const { return backendName_; }
    [[nodiscard]] int geometryBuildCount() const { return geometryBuildCount_; }
    [[nodiscard]] double resourceCreationMs() const { return resourceCreationMs_; }
    [[nodiscard]] const StudioCamera& camera() const { return camera_; }
    Q_INVOKABLE void fitDesign();
    Q_INVOKABLE void panBy(double dx, double dy);
    Q_INVOKABLE void zoomAt(double factor, double x, double y);
    // Zoom helpers; each one that jumps the view records the previous view for zoomPrevious().
    // Rectangle in SCENE mm (Y down).
    Q_INVOKABLE void zoomToSceneRect(double x0, double y0, double x1, double y1);
    Q_INVOKABLE bool zoomToSelection();
    // Absolute scale in px/mm, anchored on the viewport centre (100 % = physical size).
    Q_INVOKABLE void zoomToScale(double pxPerMm);
    Q_INVOKABLE bool zoomPrevious();
    // Centre the viewport on a SCENE point without changing the scale.
    Q_INVOKABLE void centerOnScene(double x, double y);
    // [{pos, label}] along one viewport axis, in logical px, for QML rulers (<= 60).
    Q_INVOKABLE [[nodiscard]] QVariantList rulerTicks(bool horizontal) const;
signals:
    void controllerChanged();
    void panModeChanged();
    void viewChanged();
    void zoomRectModeChanged();
    void rubberBandChanged();
    void diagnosticsChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void viewUpdated();
    void rememberView();
    void restoreView(double scale, ScenePoint offset);
    StudioController* controller_{nullptr};
    std::shared_ptr<const StudioSceneSnapshot> scene_;
    std::vector<Target> selection_;
    std::uint64_t selectionRevision_{0};
    std::shared_ptr<const CanvasOverlays> overlays_;
    std::uint64_t overlayRevision_{0};
    struct SavedView {
        double scale;
        ScenePoint offset;
    };
    std::vector<SavedView> viewHistory_;
    bool zoomRectMode_{false};
    bool rubberActive_{false};
    QRectF rubberBand_;
    StudioCamera camera_;
    QString backendName_{QStringLiteral("Initialisation du rendu…")};
    int geometryBuildCount_{0};
    double resourceCreationMs_{0};
    bool panMode_{false};
    bool spaceDown_{false};
    bool dragging_{false};
    bool fitPending_{true};
    QPointF lastMouse_;
};

} // namespace openstitch::desktop_v2::studio
