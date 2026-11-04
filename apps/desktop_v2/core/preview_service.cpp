// SPDX-License-Identifier: Apache-2.0
#include "preview_service.hpp"

#include <map>

#include "openstitch/image/image.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_render/params.hpp"
#include "openstitch/stitch_render/raster.hpp"

namespace openstitch::desktop_v2 {

Result<PreviewImage> render_preview(std::shared_ptr<const document::Project> project,
                                    std::uint64_t revision, double pxPerMm,
                                    const TaskContext& task) {
    if (!project) {
        return fail(ErrorCategory::Internal, "Aperçu demandé sans projet");
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    auto sequence = stitch_generation::effective_sequence(*project, task);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    std::map<std::uint64_t, std::array<std::uint8_t, 3>> colours;
    bool encodable = true;
    for (const auto& e : project->embroidery_objects) {
        if (e.visible) {
            colours[e.id.value] = e.rgb;
            encodable = encodable && e.id.value <= kMaxEncodableId;
        }
    }
    const auto colourOf = [&colours](ObjectId id) -> std::optional<std::array<std::uint8_t, 3>> {
        const auto it = colours.find(id.value);
        if (it == colours.end()) {
            return std::nullopt; // objet masqué : ni cousu, ni rendu, ni sélectionnable ici
        }
        return it->second;
    };
    const auto segments =
        stitch_render::build_thread_segments(*sequence, sequence->commands.size(), colourOf);
    const stitch_render::RectMm bounds = stitch_render::segments_bounds(segments);
    if (bounds.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Aucun point cousu visible");
    }
    constexpr double kThreadMm = 0.35;
    const stitch_render::RectMm region{bounds.x0 - kThreadMm, bounds.y0 - kThreadMm,
                                       bounds.x1 + kThreadMm, bounds.y1 + kThreadMm};
    const auto view = stitch_render::plan_view(region, pxPerMm, 40'000'000);
    if (view.width <= 0 || view.height <= 0) {
        return fail(ErrorCategory::OperationImpossible, "Fenêtre d'aperçu vide");
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    stitch_render::RenderParams params;
    params.thread_width_mm = kThreadMm;
    params.fabric_rgb = {245, 243, 238}; // toile écrue, lisible en thèmes clair et sombre
    params.fabric_opaque = true;
    params.fabric_texture = stitch_render::FabricTexture::Plain;
    params.quality = stitch_render::Quality::High;
    const auto raster =
        stitch_render::render_threads(segments, stitch_render::sanitized(params), view);
    image::Image img;
    img.width = raster.width;
    img.height = raster.height;
    img.rgba = raster.rgba;
    auto png = image::encode_png(img);
    if (!png) {
        return std::unexpected(png.error());
    }
    if (task.cancelled()) {
        return cancelled_error(); // rendu terminé mais périmé : jamais livré
    }
    PreviewImage out;
    out.png = std::move(*png);
    out.width = raster.width;
    out.height = raster.height;
    out.px_per_mm = view.px_per_mm;
    out.origin_x_mm = view.rect.x0;
    out.origin_y_mm = view.rect.y0;
    out.revision = revision;
    if (encodable) {
        // Second jeu de segments : la « couleur » porte l'identifiant de l'objet (même géométrie).
        const auto idSegments = stitch_render::build_thread_segments(
            *sequence, sequence->commands.size(),
            [&colours](ObjectId id) -> std::optional<std::array<std::uint8_t, 3>> {
                if (!colours.contains(id.value)) {
                    return std::nullopt;
                }
                return encode_id(id.value);
            });
        out.hits = std::make_shared<const HitIndex>(HitIndex::build(idSegments));
    }
    return out;
}

} // namespace openstitch::desktop_v2
