// SPDX-License-Identifier: Apache-2.0
#include "thread_model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::project_ops::detail {

const document::EmbroideryObject* ThreadModel::object(const document::Project& project,
                                                      std::uint32_t id) const {
    const auto it = std::lower_bound(object_index.begin(), object_index.end(), id,
                                     [](const auto& e, std::uint32_t v) { return e.first < v; });
    if (it == object_index.end() || it->first != id) {
        return nullptr;
    }
    return &project.embroidery_objects[it->second];
}

std::size_t ThreadModel::rank_of(std::uint32_t id) const {
    const auto it = std::lower_bound(object_index.begin(), object_index.end(), id,
                                     [](const auto& e, std::uint32_t v) { return e.first < v; });
    return it == object_index.end() || it->first != id ? std::numeric_limits<std::size_t>::max()
                                                       : it->second;
}

image::Image working_image(const document::Project& project) {
    if (project.ops.empty()) {
        return project.original;
    }
    auto out = image::apply_pipeline(project.original, project.ops);
    return out ? std::move(*out) : image::Image{};
}

Result<ThreadModel> build_thread_model(const document::Project& project) {
    auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    ThreadModel model;
    model.stats = stitch::compute_stats(*sequence);
    for (std::size_t i = 0; i < project.embroidery_objects.size(); ++i) {
        if (project.embroidery_objects[i].visible) {
            model.object_index.emplace_back(
                static_cast<std::uint32_t>(project.embroidery_objects[i].id.value), i);
        }
    }
    std::sort(model.object_index.begin(), model.object_index.end());

    const auto colour_of = [&model](const std::array<std::uint8_t, 3>& rgb) -> std::uint16_t {
        const auto it = std::find(model.palette.begin(), model.palette.end(), rgb);
        if (it != model.palette.end()) {
            return static_cast<std::uint16_t>(it - model.palette.begin());
        }
        model.palette.push_back(rgb);
        return static_cast<std::uint16_t>(model.palette.size() - 1);
    };

    const auto& cmds = sequence->commands;
    bool have_prev = false;
    stitch::StitchCommand prev;
    std::uint32_t order = 0;
    model.min_x = model.min_y = std::numeric_limits<double>::max();
    model.max_x = model.max_y = std::numeric_limits<double>::lowest();
    for (const auto& c : cmds) {
        if (c.type != stitch::CommandType::Stitch) {
            have_prev = false;
            continue;
        }
        if (have_prev && prev.source == c.source) {
            const auto id = static_cast<std::uint32_t>(c.source.value);
            if (const auto* obj = model.object(project, id)) {
                ThreadSeg s;
                s.ax = static_cast<double>(prev.pos.x.value) / 1000.0;
                s.ay = static_cast<double>(prev.pos.y.value) / 1000.0;
                s.bx = static_cast<double>(c.pos.x.value) / 1000.0;
                s.by = static_cast<double>(c.pos.y.value) / 1000.0;
                s.object = id;
                s.order = order++;
                s.colour = colour_of(obj->rgb);
                s.pass = c.pass;
                model.segs.push_back(s);
                for (const double x : {s.ax, s.bx}) {
                    model.min_x = std::min(model.min_x, x);
                    model.max_x = std::max(model.max_x, x);
                }
                for (const double y : {s.ay, s.by}) {
                    model.min_y = std::min(model.min_y, y);
                    model.max_y = std::max(model.max_y, y);
                }
            }
        }
        prev = c;
        have_prev = true;
    }
    if (model.segs.empty()) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "analyse impossible : aucun point cousu visible",
                                     {}});
    }
    return model;
}

ThreadRaster rasterize_threads(const ThreadModel& model, int width, int height, double mm_per_px,
                               double thread_mm) {
    ThreadRaster r;
    r.width = width;
    r.height = height;
    r.mm_per_px = mm_per_px;
    const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    r.covered.assign(n, 0);
    r.colour.assign(n, 0);
    r.top_object.assign(n, 0);
    const double radius = std::max(0.5, thread_mm / 2.0 / mm_per_px);
    const double r2 = radius * radius;
    for (const ThreadSeg& s : model.segs) {
        const double ax = mm_to_px_x(s.ax, width, mm_per_px);
        const double ay = mm_to_px_y(s.ay, height, mm_per_px);
        const double bx = mm_to_px_x(s.bx, width, mm_per_px);
        const double by = mm_to_px_y(s.by, height, mm_per_px);
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min(ax, bx) - radius)));
        const int x1 = std::min(width - 1, static_cast<int>(std::ceil(std::max(ax, bx) + radius)));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min(ay, by) - radius)));
        const int y1 = std::min(height - 1, static_cast<int>(std::ceil(std::max(ay, by) + radius)));
        if (x0 > x1 || y0 > y1) {
            continue;
        }
        const double dx = bx - ax;
        const double dy = by - ay;
        const double len2 = dx * dx + dy * dy;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const double px = x + 0.5;
                const double py = y + 0.5;
                double t = len2 > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
                t = std::clamp(t, 0.0, 1.0);
                const double qx = ax + t * dx - px;
                const double qy = ay + t * dy - py;
                if (qx * qx + qy * qy <= r2) {
                    const std::size_t i = static_cast<std::size_t>(y) * width + x;
                    r.covered[i] = 1;
                    r.colour[i] = static_cast<std::uint16_t>(s.colour + 1);
                    r.top_object[i] = s.object;
                }
            }
        }
    }
    return r;
}

std::vector<Component> components(const std::vector<char>& mask, int width, int height,
                                  std::size_t keep, std::size_t* total) {
    std::vector<Component> out;
    std::vector<char> seen(mask.size(), 0);
    std::vector<std::size_t> stack;
    std::size_t count = 0;
    for (int y0 = 0; y0 < height; ++y0) {
        for (int x0 = 0; x0 < width; ++x0) {
            const std::size_t start = static_cast<std::size_t>(y0) * width + x0;
            if (mask[start] == 0 || seen[start] != 0) {
                continue;
            }
            ++count;
            Component c;
            c.min_x = c.max_x = x0;
            c.min_y = c.max_y = y0;
            double sx = 0.0, sy = 0.0;
            stack.assign(1, start);
            seen[start] = 1;
            while (!stack.empty()) {
                const std::size_t cur = stack.back();
                stack.pop_back();
                const int cx = static_cast<int>(cur % static_cast<std::size_t>(width));
                const int cy = static_cast<int>(cur / static_cast<std::size_t>(width));
                ++c.area;
                sx += cx + 0.5;
                sy += cy + 0.5;
                c.min_x = std::min(c.min_x, cx);
                c.max_x = std::max(c.max_x, cx);
                c.min_y = std::min(c.min_y, cy);
                c.max_y = std::max(c.max_y, cy);
                const int nx[4] = {cx + 1, cx - 1, cx, cx};
                const int ny[4] = {cy, cy, cy + 1, cy - 1};
                for (int k = 0; k < 4; ++k) {
                    if (nx[k] < 0 || nx[k] >= width || ny[k] < 0 || ny[k] >= height) {
                        continue;
                    }
                    const std::size_t ni = static_cast<std::size_t>(ny[k]) * width + nx[k];
                    if (mask[ni] != 0 && seen[ni] == 0) {
                        seen[ni] = 1;
                        stack.push_back(ni);
                    }
                }
            }
            c.cx = sx / static_cast<double>(c.area);
            c.cy = sy / static_cast<double>(c.area);
            out.push_back(c);
        }
    }
    std::sort(out.begin(), out.end(), [](const Component& a, const Component& b) {
        if (a.area != b.area) {
            return a.area > b.area;
        }
        return std::tie(a.cy, a.cx) < std::tie(b.cy, b.cx);
    });
    if (total != nullptr) {
        *total = count;
    }
    if (out.size() > keep) {
        out.resize(keep);
    }
    return out;
}

Lab to_lab(std::uint8_t r8, std::uint8_t g8, std::uint8_t b8) {
    static const std::array<double, 256> lin = [] {
        std::array<double, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const double v = i / 255.0;
            t[static_cast<std::size_t>(i)] =
                v > 0.04045 ? std::pow((v + 0.055) / 1.055, 2.4) : v / 12.92;
        }
        return t;
    }();
    const double r = lin[r8];
    const double g = lin[g8];
    const double b = lin[b8];
    const double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    const double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    const auto f = [](double v) { return v > 0.008856 ? std::cbrt(v) : 7.787 * v + 16.0 / 116.0; };
    const double fx = f(x), fy = f(y), fz = f(z);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

double delta_e(const Lab& x, const Lab& y) {
    const double dl = x.l - y.l, da = x.a - y.a, db = x.b - y.b;
    return std::sqrt(dl * dl + da * da + db * db);
}

} // namespace openstitch::project_ops::detail
