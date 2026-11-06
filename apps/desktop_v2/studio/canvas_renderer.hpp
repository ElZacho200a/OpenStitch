// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QMatrix4x4>
#include <QSizeF>
#include <memory>
#include <vector>

#include "selection_model.hpp"
#include "studio_camera.hpp"
#include "studio_scene.hpp"

class QQuickWindow;
class QSGNode;

namespace openstitch::desktop_v2::studio {

// Render-thread input, copied during Qt Quick's synchronization phase while GUI is blocked.
// No QObject, live DocumentSession or worker-owned mutable data crosses this boundary.
struct CanvasRenderState {
    std::shared_ptr<const StudioSceneSnapshot> scene;
    StudioCamera camera;
    std::vector<Target> selection;
    std::uint64_t selectionRevision{0};
    QSizeF viewport;
    double devicePixelRatio{1};
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
