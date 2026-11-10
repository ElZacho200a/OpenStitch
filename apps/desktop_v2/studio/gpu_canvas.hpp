// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QPointF>
#include <QQuickItem>
#include <QRectF>
#include <QVariantList>
#include <functional>
#include <memory>
#include <vector>

#include "canvas_renderer.hpp"
#include "digitize_gestures.hpp"
#include "edit_controller.hpp"
#include "studio_camera.hpp"
#include "studio_controller.hpp"
#include "tool_controller.hpp"

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
    Q_PROPERTY(QRectF hoverRect READ hoverRect NOTIFY overlayChanged)
    Q_PROPERTY(QRectF ghostRect READ ghostRect NOTIFY overlayChanged)
    Q_PROPERTY(QVariantList guideLines READ guideLines NOTIFY overlayChanged)
    Q_PROPERTY(QString gestureInfo READ gestureInfo NOTIFY overlayChanged)
    // Famille F : outils de dessin et d'édition vectorielle. L'aperçu (polylignes, points) est
    // rendu en px canevas : [{kind, pts:[x0,y0,x1,y1,...]}], quelques éléments seulement.
    Q_PROPERTY(ToolController* tools READ tools WRITE setTools NOTIFY toolsChanged)
    Q_PROPERTY(QVariantList toolOverlay READ toolOverlay NOTIFY overlayChanged)
    // Famille I : gestes de numérisation (points, fondu, satin, guides). Poignées et nuage de
    // points en px canevas, élagués à la fenêtre ; jamais un objet QML par point.
    Q_PROPERTY(DigitizeGestures* digitize READ digitize WRITE setDigitize NOTIFY digitizeChanged)
    Q_PROPERTY(QVariantList handlesOverlay READ handlesOverlay NOTIFY overlayChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(bool zoomRectMode READ zoomRectMode WRITE setZoomRectMode NOTIFY zoomRectModeChanged)
    Q_PROPERTY(QRectF rubberBand READ rubberBand NOTIFY rubberBandChanged)
    Q_PROPERTY(bool canZoomPrevious READ canZoomPrevious NOTIFY viewChanged)
    Q_PROPERTY(QString backendName READ backendName NOTIFY diagnosticsChanged)
    Q_PROPERTY(int geometryBuildCount READ geometryBuildCount NOTIFY diagnosticsChanged)
    // Couches d'affichage {sourceImage, regions, vectors, stitches, selectionOverlays} (booléens).
    // Les clés absentes gardent leur valeur ; défaut : stitches et selectionOverlays seulement.
    Q_PROPERTY(QVariantMap layers READ layers WRITE setLayers NOTIFY layersChanged)
    Q_PROPERTY(double resourceCreationMs READ resourceCreationMs NOTIFY diagnosticsChanged)
    // Famille K : rendu réaliste GPU (diagnostics lus côté GUI, jamais écrits au rendu).
    Q_PROPERTY(bool rhiBackend READ rhiBackend NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool realisticActive READ realisticActive NOTIFY diagnosticsChanged)
    Q_PROPERTY(int realisticBuildCount READ realisticBuildCount NOTIFY diagnosticsChanged)
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
    [[nodiscard]] QRectF hoverRect() const;
    [[nodiscard]] QRectF ghostRect() const { return ghost_; }
    [[nodiscard]] QVariantList guideLines() const { return guides_; }
    [[nodiscard]] QString gestureInfo() const;
    [[nodiscard]] ToolController* tools() const { return tools_; }
    void setTools(ToolController* tools);
    [[nodiscard]] QVariantList toolOverlay() const;
    [[nodiscard]] DigitizeGestures* digitize() const { return digitize_; }
    void setDigitize(DigitizeGestures* digitize);
    [[nodiscard]] QVariantList handlesOverlay() const;
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
    // Couche « sourceImage » (espace Image) : image de travail positionnée en mm de scène. N'est
    // dessinée que si `layers.sourceImage` est vrai ; ne touche ni la scène ni les points.
    void setSourceImage(SourceImageView view);
    [[nodiscard]] const SourceImageView& sourceImage() const { return sourceImage_; }
    [[nodiscard]] QString backendName() const { return backendName_; }
    [[nodiscard]] int geometryBuildCount() const { return geometryBuildCount_; }
    [[nodiscard]] double resourceCreationMs() const { return resourceCreationMs_; }
    // Vrai quand le backend Qt Quick est RHI (le rendu réaliste GPU est alors disponible).
    [[nodiscard]] bool rhiBackend() const;
    [[nodiscard]] bool realisticActive() const { return realisticActive_; }
    [[nodiscard]] int realisticBuildCount() const { return realisticBuildCount_; }
    [[nodiscard]] double realisticBuildMs() const { return realisticBuildMs_; }
    [[nodiscard]] const StudioCamera& camera() const { return camera_; }
    [[nodiscard]] QVariantMap layers() const;
    void setLayers(const QVariantMap& layers);
    [[nodiscard]] const CanvasLayers& layerFlags() const { return layers_; }
    // Données immuables de la couche « regions » (publiées par l'espace Segmentation).
    void setRegionLayer(std::shared_ptr<const RegionLayerData> data);
    [[nodiscard]] const std::shared_ptr<const RegionLayerData>& regionLayer() const {
        return regionLayer_;
    }
    // Couche « vectors » (espace Vectorisation) : image immuable des formes, publiée par
    // VectorSpaceController ; `picker` reçoit (x, y, tolérance mm, ctrl) pour un clic au canevas.
    using VectorPicker = std::function<void(double, double, double, bool)>;
    void setVectorLayer(std::shared_ptr<const OverlayImage> image);
    void setVectorPicker(VectorPicker picker) { vectorPicker_ = std::move(picker); }
    [[nodiscard]] const std::shared_ptr<const OverlayImage>& vectorImage() const {
        return vectorImage_;
    }
    // État de vue transmissible entre espaces : {centerX, centerY} en mm de SCÈNE (Y vers le
    // bas) et {zoom} en px/mm. restoreViewState() est différé jusqu'à ce que le canevas ait une
    // taille et supprime l'ajustement automatique initial.
    Q_INVOKABLE [[nodiscard]] QVariantMap viewState() const;
    Q_INVOKABLE void restoreViewState(const QVariantMap& state);
    Q_INVOKABLE void fitDesign();
    // Annule le geste en cours (glisser, poignée, rectangle) ; faux s'il n'y en avait pas.
    Q_INVOKABLE bool cancelGesture();
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
    void editorChanged();
    void toolsChanged();
    void layersChanged();
    void digitizeChanged();
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
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    enum class Gesture { None, Pan, Marquee, Move, Handle, ZoomRect, Tool, Digitize };
    // 0..7 : NO N NE E SE S SO O ; 8 : rotation ; -1 : aucune.
    [[nodiscard]] int handleAt(const QPointF& p) const;
    [[nodiscard]] QPointF handlePoint(int handle, const QRectF& box) const;
    void resetGesture();
    void viewUpdated();
    void rememberView();
    void restoreView(double scale, ScenePoint offset);
    StudioController* controller_{nullptr};
    std::shared_ptr<const StudioSceneSnapshot> scene_;
    std::vector<Target> selection_;
    // Pure presentation hover; never mutates DocumentSession or the stitch model.
    std::optional<std::uint64_t> hoveredObjectId_;
    std::uint64_t selectionRevision_{0};
    std::shared_ptr<const CanvasOverlays> overlays_;
    std::uint64_t overlayRevision_{0};
    SourceImageView sourceImage_;
    struct SavedView {
        double scale;
        ScenePoint offset;
    };
    std::vector<SavedView> viewHistory_;
    bool zoomRectMode_{false};
    bool rubberActive_{false};
    QRectF rubberBand_;
    StudioCamera camera_;
    CanvasLayers layers_;
    std::shared_ptr<const RegionLayerData> regionLayer_;
    std::shared_ptr<const OverlayImage> vectorImage_;
    VectorPicker vectorPicker_;
    bool viewPending_{false};
    double pendingZoom_{1.0}, pendingX_{0.0}, pendingY_{0.0};
    void applyPendingView();
    QString backendName_{QStringLiteral("Initialisation du rendu…")};
    int geometryBuildCount_{0};
    double resourceCreationMs_{0};
    int realisticBuildCount_{0};
    double realisticBuildMs_{0};
    bool realisticActive_{false};
    bool panMode_{false};
    bool spaceDown_{false};
    bool fitPending_{true};
    QPointF lastMouse_;
    EditController* editor_{nullptr};
    ToolController* tools_{nullptr};
    DigitizeGestures* digitize_{nullptr};
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
