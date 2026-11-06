// SPDX-License-Identifier: Apache-2.0
#pragma once

// Backend-independent, immutable rendering data. Coordinates cross the document/display
// boundary once: integer micrometres, Y-up -> world millimetres, Y-down. No Qt/GPU resources.

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch_render/segments.hpp"

namespace openstitch::desktop_v2 {

struct ScenePoint {
    double x{0};
    double y{0};
    bool operator==(const ScenePoint&) const = default;
};

// Deliberately matches the components of a flat, coloured vertex without depending on QSG.
// Every segment has six triangle vertices, in sewing order. Backends may split this vector
// into bounded batches without changing the immutable source or its object identities.
struct StudioVertex {
    float x{0};
    float y{0};
    std::uint8_t r{0}, g{0}, b{0}, a{255};
    bool operator==(const StudioVertex&) const = default;
};

struct StudioSegment {
    stitch_render::ThreadSegment thread;
    std::uint64_t objectId{0}; // full identity; never encoded in RGB or a JavaScript number
};

struct StudioSceneSnapshot {
    std::shared_ptr<const document::Project> project;
    std::uint64_t revision{0};
    std::size_t commandCount{0};
    double threadWidthMm{0.22};
    stitch_render::RectMm hoop;
    stitch_render::RectMm bounds; // hoop union thread geometry, suitable for fit
    std::vector<StudioSegment> segments;
    std::vector<StudioVertex> vertices;
    std::map<std::uint64_t, stitch_render::RectMm> objectBounds;

    [[nodiscard]] std::optional<std::uint64_t> pick(double xMm, double yMm,
                                                    double toleranceMm) const;
    [[nodiscard]] std::optional<stitch_render::RectMm> boundsOf(std::uint64_t id) const;
};

// Worker-thread operation, always using the effective sequence (overrides and finish passes).
// Published snapshots are const. Camera/selection changes do not call this function.
[[nodiscard]] Result<std::shared_ptr<const StudioSceneSnapshot>>
build_studio_scene(std::shared_ptr<const document::Project> project, std::uint64_t revision,
                   const TaskContext& task, double threadWidthMm = 0.22);

// Independent invalidation domains. Observe on the rendering thread only. Reset after scene
// graph/context destruction, so an unchanged CPU snapshot uploads to the new GPU resources.
struct StudioInvalidation {
    bool geometry{false};
    bool selection{false};
    bool camera{false};
};

class StudioResourceState {
public:
    [[nodiscard]] StudioInvalidation
    observe(const std::shared_ptr<const StudioSceneSnapshot>& scene,
            std::uint64_t selectionRevision, std::uint64_t cameraRevision);
    void reset();

private:
    std::shared_ptr<const StudioSceneSnapshot> scene_;
    std::uint64_t selectionRevision_{0};
    std::uint64_t cameraRevision_{0};
    bool initialized_{false};
};

} // namespace openstitch::desktop_v2
