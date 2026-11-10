// SPDX-License-Identifier: Apache-2.0
#include "studio_scene.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::desktop_v2 {
namespace {
using Clock = std::chrono::steady_clock;

double millis(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

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

constexpr double kPickCellMm = 2.0;
constexpr std::int64_t kMaxPickCells = 4'000'000;
constexpr std::int64_t kMaxPickCellsPerQuery = 4096;

bool finiteSegment(const stitch_render::ThreadSegment& s) {
    return std::isfinite(s.ax) && std::isfinite(s.ay) && std::isfinite(s.bx) && std::isfinite(s.by);
}

// Indice de cellule entier le long d'un axe, borné à [0, count - 1].
int cellIndex(double value, double origin, double inverseCell, int count) {
    const double raw = (value - origin) * inverseCell;
    if (!(raw > 0.0)) {
        return 0; // négatif, nul ou NaN : première cellule
    }
    const double last = static_cast<double>(count - 1);
    return raw >= last ? count - 1 : static_cast<int>(raw); // troncature = floor pour raw > 0
}

} // namespace

std::shared_ptr<const StudioPickGrid> build_pick_grid(const std::vector<StudioSegment>& segments) {
    auto grid = std::make_shared<StudioPickGrid>();
    grid->segmentCount = segments.size();
    double minX = std::numeric_limits<double>::infinity();
    double minY = minX;
    double maxX = -minX;
    double maxY = -minX;
    for (const auto& segment : segments) {
        const auto& t = segment.thread;
        if (segment.objectId == 0 || !finiteSegment(t)) {
            continue;
        }
        minX = std::min({minX, static_cast<double>(t.ax), static_cast<double>(t.bx)});
        maxX = std::max({maxX, static_cast<double>(t.ax), static_cast<double>(t.bx)});
        minY = std::min({minY, static_cast<double>(t.ay), static_cast<double>(t.by)});
        maxY = std::max({maxY, static_cast<double>(t.ay), static_cast<double>(t.by)});
    }
    if (!(minX <= maxX) || !(minY <= maxY)) {
        return grid; // aucune géométrie sélectionnable : columns == 0, pick répond « rien »
    }
    double cell = kPickCellMm;
    const auto cellsFor = [&](double size) {
        const auto cols = static_cast<std::int64_t>(std::floor((maxX - minX) / size)) + 1;
        const auto rows = static_cast<std::int64_t>(std::floor((maxY - minY) / size)) + 1;
        return std::pair{cols, rows};
    };
    while (true) {
        const auto [cols, rows] = cellsFor(cell);
        if (cols * rows <= kMaxPickCells) {
            break;
        }
        cell *= 2.0;
    }
    const auto [cols, rows] = cellsFor(cell);
    grid->originX = minX;
    grid->originY = minY;
    grid->cellMm = cell;
    grid->invCellMm = 1.0 / cell;
    grid->columns = static_cast<int>(cols);
    grid->rows = static_cast<int>(rows);

    const auto cellCount = static_cast<std::size_t>(cols * rows);
    const auto forEachCell = [&](const stitch_render::ThreadSegment& t, auto&& visit) {
        const int c0 = cellIndex(std::min(t.ax, t.bx), minX, grid->invCellMm, grid->columns);
        const int c1 = cellIndex(std::max(t.ax, t.bx), minX, grid->invCellMm, grid->columns);
        const int r0 = cellIndex(std::min(t.ay, t.by), minY, grid->invCellMm, grid->rows);
        const int r1 = cellIndex(std::max(t.ay, t.by), minY, grid->invCellMm, grid->rows);
        for (int r = r0; r <= r1; ++r) {
            for (int c = c0; c <= c1; ++c) {
                visit(static_cast<std::size_t>(r) * static_cast<std::size_t>(grid->columns) +
                      static_cast<std::size_t>(c));
            }
        }
    };
    // Passe 1 : compter ; passe 2 : remplir. Les indices de segment croissent donc chaque cellule
    // est triée sans tri explicite, et la construction est déterministe.
    std::vector<std::uint32_t> counts(cellCount + 1, 0);
    std::size_t total = 0;
    for (const auto& segment : segments) {
        if (segment.objectId == 0 || !finiteSegment(segment.thread)) {
            continue;
        }
        forEachCell(segment.thread, [&](std::size_t cellId) {
            ++counts[cellId + 1];
            ++total;
        });
    }
    // Garde-fou mémoire : une scène pathologique (segments immenses) retombe sur le balayage.
    if (total > segments.size() * 32 + 1'000'000 ||
        total > std::numeric_limits<std::uint32_t>::max()) {
        return nullptr;
    }
    for (std::size_t i = 1; i < counts.size(); ++i) {
        counts[i] += counts[i - 1];
    }
    grid->offsets = counts;
    grid->items.resize(total);
    std::vector<std::uint32_t> cursor(counts.begin(), counts.end() - 1);
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& segment = segments[i];
        if (segment.objectId == 0 || !finiteSegment(segment.thread)) {
            continue;
        }
        forEachCell(segment.thread, [&](std::size_t cellId) {
            grid->items[cursor[cellId]++] = static_cast<std::uint32_t>(i);
        });
    }
    return grid;
}

std::optional<std::uint64_t> StudioSceneSnapshot::pick(double xMm, double yMm,
                                                       double toleranceMm) const {
    if (!std::isfinite(xMm) || !std::isfinite(yMm) || !std::isfinite(toleranceMm) ||
        toleranceMm < 0) {
        return std::nullopt;
    }
    const double radius = toleranceMm + threadWidthMm / 2.0;
    const double radiusSquared = radius * radius;
    if (pickGrid && pickGrid->segmentCount == segments.size()) {
        const auto& grid = *pickGrid;
        if (grid.columns == 0) {
            return std::nullopt;
        }
        // Marge de 1 nm : l'arrondi flottant ne doit jamais exclure une cellule candidate.
        const double reach = radius + 1e-6;
        const double hi = static_cast<double>(grid.columns);
        const double hj = static_cast<double>(grid.rows);
        if (xMm + reach < grid.originX || yMm + reach < grid.originY ||
            xMm - reach > grid.originX + hi * grid.cellMm ||
            yMm - reach > grid.originY + hj * grid.cellMm) {
            return std::nullopt;
        }
        const int c0 = cellIndex(xMm - reach, grid.originX, grid.invCellMm, grid.columns);
        const int c1 = cellIndex(xMm + reach, grid.originX, grid.invCellMm, grid.columns);
        const int r0 = cellIndex(yMm - reach, grid.originY, grid.invCellMm, grid.rows);
        const int r1 = cellIndex(yMm + reach, grid.originY, grid.invCellMm, grid.rows);
        const auto queryCells = static_cast<std::int64_t>(c1 - c0 + 1) * (r1 - r0 + 1);
        if (queryCells <= kMaxPickCellsPerQuery) {
            std::optional<std::uint32_t> best;
            for (int r = r0; r <= r1; ++r) {
                for (int c = c0; c <= c1; ++c) {
                    const auto cellId =
                        static_cast<std::size_t>(r) * static_cast<std::size_t>(grid.columns) +
                        static_cast<std::size_t>(c);
                    // Indices croissants : on part de la fin et on s'arrête au premier trouvé.
                    for (auto k = grid.offsets[cellId + 1]; k > grid.offsets[cellId]; --k) {
                        const std::uint32_t index = grid.items[k - 1];
                        if (best && index <= *best) {
                            break;
                        }
                        if (distanceSquared(xMm, yMm, segments[index].thread) <= radiusSquared) {
                            best = index;
                            break;
                        }
                    }
                }
            }
            return best ? std::optional(segments[*best].objectId) : std::nullopt;
        }
    }
    for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
        if (it->objectId != 0 && distanceSquared(xMm, yMm, it->thread) <= radiusSquared) {
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
    const auto sequenceStart = Clock::now();
    auto sequence = stitch_generation::effective_sequence(*scene->project, task);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    scene->timing.effectiveSequenceMs = millis(sequenceStart);
    scene->effectiveSequence = std::make_shared<const stitch::StitchSequence>(std::move(*sequence));
    const auto& effective = *scene->effectiveSequence;
    scene->commandCount = effective.commands.size();

    const auto segmentsStart = Clock::now();
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
    scene->timing.threadSegmentsMs = millis(segmentsStart);
    const auto verticesStart = Clock::now();
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
    scene->timing.verticesMs = millis(verticesStart);
    const auto gridStart = Clock::now();
    scene->pickGrid = build_pick_grid(scene->segments);
    scene->timing.pickGridMs = millis(gridStart);
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
