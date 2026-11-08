// SPDX-License-Identifier: Apache-2.0
#include "studio_scene.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::desktop_v2 {
namespace {

void include(stitch_render::RectMm& into, const stitch_render::RectMm& other) {
    into = {std::min(into.x0, other.x0), std::min(into.y0, other.y0), std::max(into.x1, other.x1),
            std::max(into.y1, other.y1)};
}

double distanceSquared(double x, double y, const stitch_render::ThreadSegment& segment) {
    const double dx = static_cast<double>(segment.bx) - segment.ax;
    const double dy = static_cast<double>(segment.by) - segment.ay;
    const double lengthSquared = dx * dx + dy * dy;
    const double t =
        lengthSquared > 0
            ? std::clamp(((x - segment.ax) * dx + (y - segment.ay) * dy) / lengthSquared, 0.0, 1.0)
            : 0.0;
    const double ex = x - segment.ax - t * dx;
    const double ey = y - segment.ay - t * dy;
    return ex * ex + ey * ey;
}

} // namespace

std::optional<std::uint64_t> StudioSceneSnapshot::pick(double xMm, double yMm,
                                                       double toleranceMm) const {
    if (!std::isfinite(xMm) || !std::isfinite(yMm) || !std::isfinite(toleranceMm) ||
        toleranceMm < 0) {
        return std::nullopt;
    }
    const double radius = toleranceMm + threadWidthMm / 2.0;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        if (it->objectId != 0 && distanceSquared(xMm, yMm, it->thread) <= radius * radius) {
            return it->objectId; // last sewn object wins, including overlapping colours
        }
    }
    return std::nullopt;
}

std::optional<stitch_render::RectMm> StudioSceneSnapshot::boundsOf(std::uint64_t id) const {
    const auto found = objectBounds.find(id);
    return found != objectBounds.end() ? std::optional(found->second) : std::nullopt;
}

Result<std::shared_ptr<const StudioSceneSnapshot>>
build_studio_scene(std::shared_ptr<const document::Project> project, std::uint64_t revision,
                   const TaskContext& task, double threadWidthMm) {
    if (!project) {
        return fail(ErrorCategory::Internal, "Scène Studio demandée sans projet");
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    if (!std::isfinite(threadWidthMm) || threadWidthMm <= 0 || threadWidthMm > 10) {
        return fail(ErrorCategory::UserInput, "Largeur de fil Studio invalide");
    }
    auto scene = std::make_shared<StudioSceneSnapshot>();
    scene->project = std::move(project);
    scene->revision = revision;
    scene->threadWidthMm = threadWidthMm;
    const double halfW = scene->project->canvas.width.value / 2000.0;
    const double halfH = scene->project->canvas.height.value / 2000.0;
    scene->hoop = {-halfW, -halfH, halfW, halfH};
    scene->bounds = scene->hoop;

    std::map<std::uint64_t, std::array<std::uint8_t, 3>> colours;
    for (const auto& object : scene->project->embroidery_objects) {
        if (object.visible && scene->project->calqueVisible(object)) {
            colours.emplace(object.id.value, object.rgb);
        }
    }
    // Empty/all-hidden documents still publish a valid empty scene, clearing stale GPU data.
    if (colours.empty() && !scene->project->imported_design) {
        return std::shared_ptr<const StudioSceneSnapshot>(std::move(scene));
    }
    auto sequence = stitch_generation::effective_sequence(*scene->project, task);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    scene->effectiveSequence =
        std::make_shared<const stitch::StitchSequence>(std::move(*sequence));
    const auto& effective = *scene->effectiveSequence;
    scene->commandCount = effective.commands.size();

    // Capture the original, full object identity for every emitted segment. The existing
    // segment builder remains the single authority for jump/trim/stop/source continuity.
    // A machine import has no editable objects; its unassigned source 0 remains unpickable.
    std::vector<std::uint64_t> identities;
    identities.reserve(effective.commands.size());
    const auto appendThreads = [&](const stitch::StitchSequence& part,
                                   std::optional<std::array<std::uint8_t, 3>> importedRgb) {
        return stitch_render::build_thread_segments(
            part, part.commands.size(),
            [&](ObjectId source) -> std::optional<std::array<std::uint8_t, 3>> {
                const auto found = colours.find(source.value);
                if (found != colours.end()) {
                    identities.push_back(source.value);
                    return found->second;
                }
                if (source.value == 0 && importedRgb) {
                    identities.push_back(0);
                    return importedRgb;
                }
                return std::nullopt;
            });
    };
    std::vector<stitch_render::ThreadSegment> threads;
    if (scene->project->imported_design) {
        // Colour-change boundaries survive the effective sequence's finish passes. Use their
        // order, not the original command indices (locks/trims may change those indices).
        const auto& blocks = scene->project->imported_design->color_blocks;
        std::size_t blockIndex = 0;
        std::size_t begin = 0;
        for (std::size_t end = 0; end <= effective.commands.size(); ++end) {
            if ((end % 4096) == 0 && task.cancelled()) {
                return cancelled_error();
            }
            if (end < effective.commands.size() &&
                effective.commands[end].type != stitch::CommandType::ColorChange) {
                continue;
            }
            stitch::StitchSequence part;
            part.commands.assign(effective.commands.begin() + static_cast<std::ptrdiff_t>(begin),
                                 effective.commands.begin() + static_cast<std::ptrdiff_t>(end));
            const auto rgb = blockIndex < blocks.size()
                                 ? blocks[blockIndex].rgb
                                 : std::array<std::uint8_t, 3>{220, 220, 220};
            const auto added = appendThreads(part, rgb);
            threads.insert(threads.end(), added.begin(), added.end());
            begin = end + 1;
            ++blockIndex;
        }
    } else {
        threads = appendThreads(effective, std::nullopt);
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    scene->segments.reserve(threads.size());
    scene->vertices.reserve(threads.size() * 6);
    const double halfWidth = threadWidthMm / 2.0;
    for (std::size_t i = 0; i < threads.size(); ++i) {
        if ((i % 1024) == 0) {
            task.report(i, threads.size());
            if (task.cancelled()) {
                return cancelled_error();
            }
        }
        const auto& thread = threads[i];
        scene->segments.push_back({thread, identities[i]});
        const double dx = static_cast<double>(thread.bx) - thread.ax;
        const double dy = static_cast<double>(thread.by) - thread.ay;
        const double length = std::hypot(dx, dy);
        const double nx = -dy * halfWidth / length;
        const double ny = dx * halfWidth / length;
        const auto vertex = [&](double x, double y) {
            return StudioVertex{static_cast<float>(x), static_cast<float>(y), thread.rgb[0],
                                thread.rgb[1],         thread.rgb[2],         255};
        };
        const StudioVertex a = vertex(thread.ax + nx, thread.ay + ny);
        const StudioVertex b = vertex(thread.ax - nx, thread.ay - ny);
        const StudioVertex c = vertex(thread.bx + nx, thread.by + ny);
        const StudioVertex d = vertex(thread.bx - nx, thread.by - ny);
        scene->vertices.insert(scene->vertices.end(), {a, b, c, c, b, d});
        // Include width, including flat one-segment designs whose centre-line bounds are empty.
        const stitch_render::RectMm extent{
            std::min(thread.ax, thread.bx) - halfWidth, std::min(thread.ay, thread.by) - halfWidth,
            std::max(thread.ax, thread.bx) + halfWidth, std::max(thread.ay, thread.by) + halfWidth};
        if (scene->bounds.empty()) {
            scene->bounds = extent;
        } else {
            include(scene->bounds, extent);
        }
        if (identities[i] != 0) {
            auto [found, inserted] = scene->objectBounds.try_emplace(identities[i], extent);
            if (!inserted) {
                include(found->second, extent);
            }
        }
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    task.report(threads.size(), threads.size());
    if (task.cancelled()) {
        return cancelled_error();
    }
    return std::shared_ptr<const StudioSceneSnapshot>(std::move(scene));
}

StudioInvalidation
StudioResourceState::observe(const std::shared_ptr<const StudioSceneSnapshot>& scene,
                             std::uint64_t selectionRevision, std::uint64_t cameraRevision) {
    const bool geometry = !initialized_ || scene != scene_;
    const StudioInvalidation result{geometry, geometry || selectionRevision != selectionRevision_,
                                    !initialized_ || cameraRevision != cameraRevision_};
    scene_ = scene;
    selectionRevision_ = selectionRevision;
    cameraRevision_ = cameraRevision;
    initialized_ = true;
    return result;
}

void StudioResourceState::reset() {
    scene_.reset();
    initialized_ = false;
}

} // namespace openstitch::desktop_v2
