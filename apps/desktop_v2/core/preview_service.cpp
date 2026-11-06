// SPDX-License-Identifier: Apache-2.0
#include "preview_service.hpp"

#include <algorithm>
#include <map>

#include "openstitch/image/image.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_render/params.hpp"
#include "openstitch/stitch_render/raster.hpp"

namespace openstitch::desktop_v2 {

Result<PreviewImage> render_preview(std::shared_ptr<const document::Project> project,
                                    std::uint64_t revision, double pxPerMm, const TaskContext& task,
                                    std::optional<std::size_t> upToCommand) {
    RenderSettings settings;
    settings.pxPerMm = pxPerMm;
    return render_preview(std::move(project), revision, settings, task, upToCommand);
}

Result<PreviewImage> render_preview(std::shared_ptr<const document::Project> project,
                                    std::uint64_t revision, const RenderSettings& rawSettings,
                                    const TaskContext& task,
                                    std::optional<std::size_t> upToCommand) {
    const RenderSettings settings = clamped(rawSettings);
    const double pxPerMm = settings.pxPerMm;
    if (!project) {
        return fail(ErrorCategory::Internal, "Aperçu demandé sans projet");
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    // Document sans objet (création à partir de zéro) : pas de séquence, seulement le cadre.
    stitch::StitchSequence emptySequence;
    stitch::StitchSequence* sequence = &emptySequence;
    Result<stitch::StitchSequence> computed = fail(ErrorCategory::Internal, "non calculée");
    // Aucun objet visible (tous masqués, ou dans des calques masqués) : le moteur refuse de générer
    // ; l'aperçu montre alors simplement le cadre vide, jamais une image périmée.
    const bool anyVisible = std::ranges::any_of(project->embroidery_objects, [&](const auto& o) {
        return o.visible && project->calqueVisible(o);
    });
    if (anyVisible) {
        computed = stitch_generation::effective_sequence(*project, task);
        if (!computed) {
            return std::unexpected(computed.error());
        }
        sequence = &*computed;
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
    // Fenêtre = cadre de broderie ∪ points cousus : le motif est toujours vu dans son cadre, et un
    // document sans point (création à partir de zéro) a quand même un aperçu.
    const double hoopHalfW = project->canvas.width.value / 2000.0;
    const double hoopHalfH = project->canvas.height.value / 2000.0;
    stitch_render::RectMm region{-hoopHalfW, -hoopHalfH, hoopHalfW, hoopHalfH};
    const double kThreadMm = settings.threadWidthMm;
    if (const stitch_render::RectMm bounds = stitch_render::segments_bounds(segments);
        !bounds.empty()) {
        if (region.empty()) {
            region = {bounds.x0 - kThreadMm, bounds.y0 - kThreadMm, bounds.x1 + kThreadMm,
                      bounds.y1 + kThreadMm};
        } else {
            region.x0 = std::min(region.x0, bounds.x0 - kThreadMm);
            region.y0 = std::min(region.y0, bounds.y0 - kThreadMm);
            region.x1 = std::max(region.x1, bounds.x1 + kThreadMm);
            region.y1 = std::max(region.y1, bounds.y1 + kThreadMm);
        }
    }
    if (region.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Aucun cadre ni point à afficher");
    }
    const auto view = stitch_render::plan_view(region, pxPerMm, 40'000'000);
    if (view.width <= 0 || view.height <= 0) {
        return fail(ErrorCategory::OperationImpossible, "Fenêtre d'aperçu vide");
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    // Simulation : seuls les premiers points sont dessinés (cadre et sélection : séquence
    // complète).
    const auto drawn = upToCommand && *upToCommand < sequence->commands.size()
                           ? stitch_render::build_thread_segments(*sequence, *upToCommand, colourOf)
                           : segments;
    stitch_render::RenderParams params;
    params.thread_width_mm = kThreadMm;
    params.relief = settings.relief;
    params.sheen = settings.sheen;
    params.twist = settings.twist;
    params.shadow = settings.shadow;
    params.fabric_rgb =
        settings.fabricRgb; // défaut : toile écrue, lisible en thèmes clair et sombre
    params.fabric_opaque = true;
    params.fabric_texture = settings.fabricWeave ? stitch_render::FabricTexture::Weave
                                                 : stitch_render::FabricTexture::Plain;
    params.fabric_relief = settings.fabricRelief;
    params.quality =
        settings.highQuality ? stitch_render::Quality::High : stitch_render::Quality::Fast;
    const auto raster =
        stitch_render::render_threads(drawn, stitch_render::sanitized(params), view);
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
    out.hoop_half_w_mm = hoopHalfW;
    out.hoop_half_h_mm = hoopHalfH;
    out.command_count = sequence->commands.size();
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
