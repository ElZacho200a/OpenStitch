// SPDX-License-Identifier: Apache-2.0
#include "view_tools.hpp"

#include <cmath>
#include <numbers>

#include "openstitch/core/atomic_file.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_render/segments.hpp"
#include "openstitch/stitch_render/thread_scene.hpp"
#include "render_settings.hpp"

namespace openstitch::desktop_v2 {

Measurement measure(PointMm from, PointMm to) {
    Measurement m;
    m.dxMm = to.x - from.x;
    m.dyMm = to.y - from.y;
    m.distanceMm = std::hypot(m.dxMm, m.dyMm);
    m.angleDeg = m.distanceMm > 0.0 ? std::atan2(m.dyMm, m.dxMm) * 180.0 / std::numbers::pi : 0.0;
    return m;
}

GridLines gridLines(const PreviewImage& frame, double spacingMm) {
    GridLines out;
    if (!(spacingMm > 0.0) || !std::isfinite(spacingMm) || frame.px_per_mm <= 0.0 ||
        frame.width <= 0 || frame.height <= 0) {
        return out;
    }
    const double x0 = frame.pxToMmX(0);
    const double x1 = frame.pxToMmX(frame.width);
    const double y0 = frame.pxToMmY(0);
    const double y1 = frame.pxToMmY(frame.height);
    const double cols = (x1 - x0) / spacingMm;
    const double rows = (y1 - y0) / spacingMm;
    if (cols + rows > static_cast<double>(kMaxGridLines)) {
        return out;
    }
    // Multiples de l'écart par rapport à l'origine (0, 0) du modèle, bords exclus.
    for (long k = static_cast<long>(std::ceil(x0 / spacingMm)); k * spacingMm <= x1; ++k) {
        out.verticalPx.push_back(frame.mmToPxX(static_cast<double>(k) * spacingMm));
    }
    for (long k = static_cast<long>(std::ceil(y0 / spacingMm)); k * spacingMm <= y1; ++k) {
        out.horizontalPx.push_back(frame.mmToPxY(static_cast<double>(k) * spacingMm));
    }
    return out;
}

Result<void> export_preview_png(std::shared_ptr<const document::Project> project,
                                const std::filesystem::path& path, double pxPerMm,
                                const TaskContext& task) {
    if (!(pxPerMm >= kMinPxPerMm && pxPerMm <= kMaxPxPerMm)) {
        return fail(ErrorCategory::UserInput, "Résolution hors limites",
                    "Choisissez entre 1 et 12 pixels par millimètre.");
    }
    auto preview = render_preview(std::move(project), 0, pxPerMm, task);
    if (!preview) {
        return std::unexpected(preview.error());
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    return write_file_atomic(path, std::span<const std::uint8_t>(preview->png));
}

Result<std::size_t> export_thread_scene(std::shared_ptr<const document::Project> project,
                                        const std::filesystem::path& path, double threadWidthMm,
                                        const TaskContext& task) {
    if (!project) {
        return fail(ErrorCategory::Internal, "Rendu photo demandé sans projet");
    }
    std::map<std::uint64_t, std::array<std::uint8_t, 3>> colours;
    for (const auto& e : project->embroidery_objects) {
        if (e.visible && project->calqueVisible(e)) {
            colours[e.id.value] = e.rgb;
        }
    }
    if (colours.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Aucun objet visible à rendre");
    }
    const auto sequence = stitch_generation::effective_sequence(*project, task);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    const auto colourOf = [&colours](ObjectId id) -> std::optional<std::array<std::uint8_t, 3>> {
        const auto it = colours.find(id.value);
        if (it == colours.end()) {
            return std::nullopt;
        }
        return it->second;
    };
    const auto segments =
        stitch_render::build_thread_segments(*sequence, sequence->commands.size(), colourOf);
    if (segments.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Aucun point cousu à rendre");
    }
    const std::string json = stitch_render::thread_scene_json(segments, threadWidthMm);
    if (auto written = write_file_atomic(
            path, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(json.data()),
                                                json.size()));
        !written) {
        return std::unexpected(written.error());
    }
    return segments.size();
}

Result<ImageExportPlan> plan_image_export(std::shared_ptr<const document::Project> project,
                                          double pxPerMm, const TaskContext& task) {
    if (!(pxPerMm >= kMinPxPerMm && pxPerMm <= kMaxPxPerMm)) {
        return fail(ErrorCategory::UserInput, "Résolution hors limites",
                    "Choisissez entre 1 et 12 pixels par millimètre.");
    }
    RenderSettings settings;
    settings.pxPerMm = pxPerMm;
    auto plan = plan_preview(std::move(project), settings, task);
    if (!plan) {
        return std::unexpected(plan.error());
    }
    ImageExportPlan out;
    out.width = plan->width;
    out.height = plan->height;
    out.pxPerMm = plan->px_per_mm;
    out.reduced = plan->px_per_mm < pxPerMm * (1.0 - 1e-9);
    out.widthMm = plan->width / plan->px_per_mm;
    out.heightMm = plan->height / plan->px_per_mm;
    return out;
}

std::optional<stitch_analysis::PlacementTemplateOptions>
placement_template_options(const std::string& paper, bool landscape, const std::string& title) {
    stitch_analysis::PlacementTemplateOptions options;
    if (paper == "A3") {
        options.paper_width = Micrometers{297'000};
        options.paper_height = Micrometers{420'000};
    } else if (paper == "Letter") {
        options.paper_width = Micrometers{215'900};
        options.paper_height = Micrometers{279'400};
    } else if (paper != "A4") {
        return std::nullopt;
    }
    options.landscape = landscape;
    options.title = title;
    return options;
}

Result<TemplateExportPlan>
plan_placement_template(const document::Project& project,
                        const stitch_analysis::PlacementTemplateOptions& options,
                        const TaskContext& task) {
    auto sequence = stitch_generation::effective_sequence(project, task);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    if (sequence->commands.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Générez d'abord des points de broderie.");
    }
    const auto layout = stitch_analysis::make_placement_template(*sequence, options);
    TemplateExportPlan out;
    out.pages = layout.pages.size();
    out.columns = layout.columns;
    out.rows = layout.rows;
    out.designWidthMm = to_millimeters(layout.design_width).value;
    out.designHeightMm = to_millimeters(layout.design_height).value;
    out.pageWidthMm = to_millimeters(layout.page_width).value;
    out.pageHeightMm = to_millimeters(layout.page_height).value;
    return out;
}

} // namespace openstitch::desktop_v2
