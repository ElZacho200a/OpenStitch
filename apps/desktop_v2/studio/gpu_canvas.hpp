// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QPointF>
#include <QQuickItem>
#include <QRectF>
#include <QVariantList>
#include <memory>
#include <vector>

#include "edit_controller.hpp"
#include "studio_camera.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class GpuCanvas : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(
        StudioController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(bool panMode READ panMode WRITE setPanMode NOTIFY panModeChanged)
    Q_PROPERTY(EditController* editor READ editor WRITE setEditor NOTIFY editorChanged)
    // Surcouches (px canevas) : rectangle de sélection, boîte des poignées, fantôme du glisser,
    // repères d'accrochage {x1,y1,x2,y2}. Peu d'éléments QML, jamais un par point.
    Q_PROPERTY(QRectF marquee READ marquee NOTIFY overlayChanged)
    Q_PROPERTY(QRectF selectionRect READ selectionRect NOTIFY overlayChanged)
    Q_PROPERTY(QRectF ghostRect READ ghostRect NOTIFY overlayChanged)
    Q_PROPERTY(QVariantList guideLines READ guideLines NOTIFY overlayChanged)
    Q_PROPERTY(QString gestureInfo READ gestureInfo NOTIFY overlayChanged)
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
    [[nodiscard]] EditController* editor() const { return editor_; }
    void setEditor(EditController* editor);
    [[nodiscard]] QRectF marquee() const { return marquee_; }
    [[nodiscard]] QRectF selectionRect() const;
    [[nodiscard]] QRectF ghostRect() const { return ghost_; }
    [[nodiscard]] QVariantList guideLines() const { return guides_; }
    [[nodiscard]] QString gestureInfo() const { return gestureInfo_; }
    [[nodiscard]] double zoom() const { return camera_.scale(); }
    [[nodiscard]] QString backendName() const { return backendName_; }
    [[nodiscard]] int geometryBuildCount() const { return geometryBuildCount_; }
    [[nodiscard]] double resourceCreationMs() const { return resourceCreationMs_; }
    [[nodiscard]] const StudioCamera& camera() const { return camera_; }
    Q_INVOKABLE void fitDesign();
    // Annule le geste en cours (glisser, poignée, rectangle) ; faux s'il n'y en avait pas.
    Q_INVOKABLE bool cancelGesture();
    Q_INVOKABLE void panBy(double dx, double dy);
    Q_INVOKABLE void zoomAt(double factor, double x, double y);
signals:
    void controllerChanged();
    void panModeChanged();
    void viewChanged();
    void diagnosticsChanged();
    void editorChanged();
    void overlayChanged();
    // Clic droit : position canevas et identifiant de l'objet touché (vide = fond).
    void contextMenuRequested(double x, double y, const QString& objectId);

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
    void hoverMoveEvent(QHoverEvent* event) override;

private:
    enum class Gesture { None, Pan, Marquee, Move, Handle };
    // 0..7 : NO N NE E SE S SO O ; 8 : rotation ; -1 : aucune.
    [[nodiscard]] int handleAt(const QPointF& p) const;
    [[nodiscard]] QPointF handlePoint(int handle, const QRectF& box) const;
    void resetGesture();
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
    bool fitPending_{true};
    QPointF lastMouse_;
    EditController* editor_{nullptr};
    Gesture gesture_{Gesture::None};
    bool gestureActive_{false}; // seuil de 3 px franchi
    QPointF pressPos_;
    int handle_{-1};
    QRectF handleBox_;
    double handleFactor_{1.0};
    double handleAngle_{0.0};
    double moveDx_{0.0}, moveDy_{0.0}; // mm, corrigés par l'accrochage
    QRectF marquee_, ghost_;
    QVariantList guides_;
    QString gestureInfo_;
};

} // namespace openstitch::desktop_v2::studio
