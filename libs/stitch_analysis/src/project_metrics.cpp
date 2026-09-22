// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/project_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <vector>

#include "openstitch/geometry/boolean.hpp"

namespace openstitch::stitch_analysis {

namespace {

// Distance² d'un point au segment [a, b].
double dist2_to_segment(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double qx = ax + t * dx - px;
    const double qy = ay + t * dy - py;
    return qx * qx + qy * qy;
}

} // namespace

ProjectMetrics project_metrics(const document::Project& project,
                               const stitch::StitchSequence& sequence,
                               const ProjectMetricsOptions& options) {
    ProjectMetrics m;
    std::set<std::uint64_t> seenVectors;
    for (const auto& obj : project.embroidery_objects) {
        if (!obj.visible) {
            continue;
        }
        ++m.embroidered_objects;
        if (obj.is_tatami()) {
            const double deg = std::get<document::TatamiParams>(obj.params).angle.radians * 180.0 /
                               std::numbers::pi;
            const int d =
                static_cast<int>(std::lround(std::fmod(std::fmod(deg, 180.0) + 180.0, 180.0))) %
                180;
            ++m.fill_angles_deg[d];
        }
        if (!seenVectors.insert(obj.source_vector.value).second) {
            continue;
        }
        if (const auto* vec = project.findObject(obj.source_vector)) {
            double area = 0.0;
            for (const auto& set : vec->paths) {
                area += geometry::path_set_area_um2(set) / 1e6;
            }
            if (area < options.small_object_mm2) {
                ++m.small_objects;
            }
        }
    }

    // Ventilation par type d'objet et passe (diagnostic).
    {
        std::map<std::uint64_t, std::string> kindOf;
        for (const auto& obj : project.embroidery_objects) {
            kindOf[obj.id.value] =
                obj.is_tatami()                                                     ? "tatami"
                : obj.is_satin()                                                    ? "satin"
                : std::holds_alternative<document::RunningStitchParams>(obj.params) ? "contour"
                                                                                    : "autre";
        }
        const auto passName = [](stitch::StitchPass p) {
            switch (p) {
            case stitch::StitchPass::Underlay:
                return "Underlay";
            case stitch::StitchPass::TopStitch:
                return "TopStitch";
            case stitch::StitchPass::Travel:
                return "Travel";
            case stitch::StitchPass::Lock:
                return "Lock";
            case stitch::StitchPass::Manual:
                return "Manual";
            }
            return "?";
        };
        const auto& cmds = sequence.commands;
        bool inMove = false;
        bool hasNeedle = false;
        Vec2um needle{};
        for (const auto& c : cmds) {
            if (c.type == stitch::CommandType::Jump) {
                inMove = true;
            } else if (c.type == stitch::CommandType::Stitch) {
                const auto it = kindOf.find(c.source.value);
                const std::string key =
                    (it != kindOf.end() ? it->second : std::string{"?"}) + "/" + passName(c.pass);
                if (inMove) {
                    ++m.moves_by_kind[key];
                }
                if (hasNeedle && c.pass != stitch::StitchPass::Lock &&
                    length_um(c.pos - needle) < static_cast<double>(options.short_stitch.value)) {
                    ++m.short_stitches_by_kind[key];
                }
                inMove = false;
            }
            needle = c.pos;
            hasNeedle = true;
        }
    }

    if (!project.segmentation || project.segmentation->width <= 0) {
        return m;
    }
    // Couverture : repère vectoriel centré sur l'image, pixel (x, y) de centre
    // ((x + ½ − w/2)·mm, (h/2 − y − ½)·mm) -- même convention que la
    // vectorisation.
    const auto& seg = *project.segmentation;
    const int w = seg.width;
    const int h = seg.height;
    const double pxUm = project.mm_per_px.value * 1000.0;
    std::vector<char> target(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    std::size_t targetCount = 0;
    for (std::size_t i = 0; i < target.size(); ++i) {
        const std::uint32_t l = seg.labels[i];
        if (l == 0 || !seg.region_slots[l - 1]) {
            continue;
        }
        if (options.excluded_rgb && seg.region_slots[l - 1]->rgb == *options.excluded_rgb) {
            continue;
        }
        target[i] = 1;
        ++targetCount;
    }
    if (targetCount == 0) {
        return m;
    }
    const double r = static_cast<double>(options.coverage_width.value) / 2.0;
    const double r2 = r * r;
    std::vector<char> covered(target.size(), 0);
    const auto toPx = [&](double xUm, double yUm) {
        return std::pair{xUm / pxUm + w / 2.0 - 0.5, h / 2.0 - yUm / pxUm - 0.5};
    };
    const auto& cmds = sequence.commands;
    for (std::size_t i = 1; i < cmds.size(); ++i) {
        if (cmds[i].type != stitch::CommandType::Stitch ||
            cmds[i - 1].type != stitch::CommandType::Stitch) {
            continue;
        }
        const double ax = cmds[i - 1].pos.x.value;
        const double ay = cmds[i - 1].pos.y.value;
        const double bx = cmds[i].pos.x.value;
        const double by = cmds[i].pos.y.value;
        const auto [pax, pay] = toPx(std::min(ax, bx) - r, std::max(ay, by) + r);
        const auto [pbx, pby] = toPx(std::max(ax, bx) + r, std::min(ay, by) - r);
        const int x0 = std::max(0, static_cast<int>(std::floor(pax)));
        const int x1 = std::min(w - 1, static_cast<int>(std::ceil(pbx)));
        const int y0 = std::max(0, static_cast<int>(std::floor(pay)));
        const int y1 = std::min(h - 1, static_cast<int>(std::ceil(pby)));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                        static_cast<std::size_t>(x);
                if (!target[idx] || covered[idx]) {
                    continue;
                }
                const double cx = (x + 0.5 - w / 2.0) * pxUm;
                const double cy = (h / 2.0 - y - 0.5) * pxUm;
                if (dist2_to_segment(cx, cy, ax, ay, bx, by) <= r2) {
                    covered[idx] = 1;
                }
            }
        }
    }
    std::size_t uncovered = 0;
    for (std::size_t i = 0; i < target.size(); ++i) {
        uncovered += (target[i] && !covered[i]) ? 1 : 0;
    }
    m.uncovered_ratio = static_cast<double>(uncovered) / static_cast<double>(targetCount);
    return m;
}

} // namespace openstitch::stitch_analysis
