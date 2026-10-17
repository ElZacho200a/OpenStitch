// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/render.hpp"

#include <algorithm>
#include <map>
#include <optional>

#include "openstitch/image/image.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_render/params.hpp"
#include "openstitch/stitch_render/raster.hpp"
#include "openstitch/stitch_render/segments.hpp"

namespace openstitch::project_ops {

Result<RenderResult> render_realistic_png(const document::Project& project,
                                          const RenderOptions& options) {
    auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    std::map<std::uint64_t, std::array<std::uint8_t, 3>> colours;
    for (const auto& e : project.embroidery_objects) {
        if (e.visible) {
            colours[e.id.value] = e.rgb;
        }
    }
    const auto segments = stitch_render::build_thread_segments(
        *sequence, sequence->commands.size(),
        [&colours](ObjectId id) -> std::optional<std::array<std::uint8_t, 3>> {
            const auto it = colours.find(id.value);
            if (it == colours.end()) {
                return std::nullopt;
            }
            return it->second;
        });
    const stitch_render::RectMm bounds = stitch_render::segments_bounds(segments);
    if (bounds.empty()) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "rendu impossible : aucun point cousu visible",
                                     {}});
    }
    const double cx0 = std::clamp(options.crop[0], 0.0, 1.0);
    const double cy0 = std::clamp(options.crop[1], 0.0, 1.0);
    const double cx1 = std::clamp(options.crop[2], 0.0, 1.0);
    const double cy1 = std::clamp(options.crop[3], 0.0, 1.0);
    if (!(cx1 > cx0) || !(cy1 > cy0)) {
        return std::unexpected(
            Error{ErrorCategory::UserInput, "zone de rendu vide : x1 > x0 et y1 > y0 requis", {}});
    }
    // Marge d'un demi-fil pour ne pas rogner les extrémités.
    const double pad = options.thread_width_mm;
    stitch_render::RectMm region;
    region.x0 = bounds.x0 + bounds.width() * cx0 - (cx0 <= 0.0 ? pad : 0.0);
    region.x1 = bounds.x0 + bounds.width() * cx1 + (cx1 >= 1.0 ? pad : 0.0);
    region.y0 = bounds.y0 + bounds.height() * cy0 - (cy0 <= 0.0 ? pad : 0.0);
    region.y1 = bounds.y0 + bounds.height() * cy1 + (cy1 >= 1.0 ? pad : 0.0);
    const auto view = stitch_render::plan_view(region, options.px_per_mm, options.max_pixels);
    if (view.width <= 0 || view.height <= 0) {
        return std::unexpected(
            Error{ErrorCategory::OperationImpossible, "rendu impossible : fenêtre vide", {}});
    }
    stitch_render::RenderParams params;
    params.thread_width_mm = options.thread_width_mm;
    params.fabric_rgb = options.fabric_rgb;
    params.fabric_opaque = true;
    params.fabric_texture = stitch_render::FabricTexture::Plain;
    params.quality = stitch_render::Quality::High;
    const auto raster =
        stitch_render::render_threads(segments, stitch_render::sanitized(params), view);
    image::Image img;
    img.width = raster.width;
    img.height = raster.height;
    img.rgba = raster.rgba; // opaque : prémultiplié == non prémultiplié
    auto png = image::encode_png(img);
    if (!png) {
        return std::unexpected(png.error());
    }
    RenderResult out;
    out.png = std::move(*png);
    out.width = raster.width;
    out.height = raster.height;
    out.px_per_mm = view.px_per_mm;
    return out;
}

} // namespace openstitch::project_ops
