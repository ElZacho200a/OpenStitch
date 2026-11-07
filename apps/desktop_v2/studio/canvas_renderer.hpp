// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QImage>
#include <QMatrix4x4>
#include <QRectF>
#include <QSizeF>
#include <limits>
#include <memory>
#include <vector>

#include "selection_model.hpp"
#include "studio_camera.hpp"
#include "studio_scene.hpp"

class QQuickWindow;
class QSGNode;

namespace openstitch::desktop_v2::studio {

// Display-only layers drawn over the immutable scene (grid, guides, analysis maps, markers,
// simulation cursor). Immutable once published: a change publishes a new instance and bumps the
// overlay revision, so pan/zoom never rebuild stitch geometry. Coordinates are scene mm (Y down).
struct OverlayImage {
    QImage image; // premultiplied RGBA, row 0 = top
    double x0{0}, y0{0}, widthMm{0}, heightMm{0};
};
struct OverlayMarker {
    double x{0}, y{0};
    QColor color{229, 99, 137};
};
struct OverlayJump {
    double ax{0}, ay{0}, bx{0}, by{0};
};
// CX12: immutable document-space display data, independent from camera and display settings.
struct ComparisonLayers {
    OverlayImage source, segmentation;
    std::vector<OverlayJump> vectors, order;
};
struct ComparisonDisplay {
    bool source{false}, segmentation{false}, vectors{false}, order{false}, curtain{false};
    double opacity{1.0}, position{0.5};
};
[[nodiscard]] QRectF comparisonSourceClip(const ComparisonLayers& layers,
                                          const ComparisonDisplay& display);
struct CanvasOverlays {
    static constexpr std::size_t all = std::numeric_limits<std::size_t>::max();
    bool showStitches{true};
    bool showHoop{true};
    bool grid{false};
    double gridStepMm{10.0};
    QColor guideColor{0, 170, 220};
    std::vector<double> guidesX; // vertical guides, scene x
    std::vector<double> guidesY; // horizontal guides, scene y
    std::vector<OverlayImage> images;
    std::vector<OverlayMarker> markers;
    std::vector<OverlayJump> jumps; // drawn when non-empty
    std::shared_ptr<const ComparisonLayers> comparison;
    ComparisonDisplay comparisonDisplay;
    std::size_t visibleSegments{all}; // simulation: first N segments of the scene
};

// Render-thread input, copied during Qt Quick's synchronization phase while GUI is blocked.
// No QObject, live DocumentSession or worker-owned mutable data crosses this boundary.
struct CanvasRenderState {
    std::shared_ptr<const StudioSceneSnapshot> scene;
    StudioCamera camera;
    std::vector<Target> selection;
    std::uint64_t selectionRevision{0};
    QSizeF viewport;
    double devicePixelRatio{1};
    std::shared_ptr<const CanvasOverlays> overlays;
    std::uint64_t overlayRevision{0};
};

struct CanvasRenderResult {
    QSGNode* node{nullptr};
    bool geometryCreated{false};
    double resourceMs{0}; // CPU time allocating/filling QSG resources; excludes driver upload.
};

// Backend boundary for future thread materials / QRhi integration. Qt owns node destruction
// on its render thread, including context loss. Geometry remains independent of Qt.
class CanvasRenderBackend {
public:
    virtual ~CanvasRenderBackend() = default;
    [[nodiscard]] virtual CanvasRenderResult
    synchronize(QSGNode* previous, const CanvasRenderState& state, QQuickWindow& window) const = 0;
};

[[nodiscard]] const CanvasRenderBackend& sceneGraphBackend();
[[nodiscard]] const CanvasRenderBackend& softwareBackend();

} // namespace openstitch::desktop_v2::studio
