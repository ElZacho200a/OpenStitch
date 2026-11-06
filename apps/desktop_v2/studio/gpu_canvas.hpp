// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QPointF>
#include <QQuickItem>
#include <memory>
#include <vector>

#include "studio_camera.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class GpuCanvas : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(
        StudioController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(bool panMode READ panMode WRITE setPanMode NOTIFY panModeChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY viewChanged)
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
    [[nodiscard]] QString backendName() const { return backendName_; }
    [[nodiscard]] int geometryBuildCount() const { return geometryBuildCount_; }
    [[nodiscard]] double resourceCreationMs() const { return resourceCreationMs_; }
    [[nodiscard]] const StudioCamera& camera() const { return camera_; }
    Q_INVOKABLE void fitDesign();
    Q_INVOKABLE void panBy(double dx, double dy);
    Q_INVOKABLE void zoomAt(double factor, double x, double y);
signals:
    void controllerChanged();
    void panModeChanged();
    void viewChanged();
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
    StudioController* controller_{nullptr};
    std::shared_ptr<const StudioSceneSnapshot> scene_;
    std::vector<Target> selection_;
    std::uint64_t selectionRevision_{0};
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
