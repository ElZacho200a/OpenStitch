// SPDX-License-Identifier: Apache-2.0
#include "preview_service.hpp"

#include "openstitch/project_ops/render.hpp"

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
    project_ops::RenderOptions options;
    options.px_per_mm = pxPerMm;
    options.fabric_rgb = {245, 243, 238}; // toile écrue, lisible en thèmes clair et sombre
    auto rendered = project_ops::render_realistic_png(*project, options);
    if (!rendered) {
        return std::unexpected(rendered.error());
    }
    if (task.cancelled()) {
        return cancelled_error(); // rendu terminé mais périmé : jamais livré
    }
    return PreviewImage{std::move(rendered->png), rendered->width, rendered->height,
                        rendered->px_per_mm, revision};
}

} // namespace openstitch::desktop_v2
