// SPDX-License-Identifier: Apache-2.0
#include "digitize_pipeline.hpp"

#include <string_view>

#include "openstitch/formats/dst.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::desktop_v2 {

Result<DigitizeOutput> run_digitize(const DigitizeRequest& request, const TaskContext& task) {
    DigitizeOutput out;
    project_ops::DigitizeSettings settings = request.settings;

    task.report(0, kDigitizeSteps);
    if (task.cancelled()) {
        return cancelled_error();
    }
    if (request.target_height_mm > 0.0) {
        const auto info = image::read_image_info(request.image);
        if (!info) {
            return std::unexpected(info.error());
        }
        if (info->height_px <= 0) {
            return fail(ErrorCategory::InvalidFile, "Image sans hauteur exploitable");
        }
        // mm = px * 25,4 / dpi  =>  dpi = px * 25,4 / mm
        settings.dpi = static_cast<double>(info->height_px) * 25.4 / request.target_height_mm;
    }

    task.report(1, kDigitizeSteps);
    const auto log = [&out](std::string_view line) { out.log.emplace_back(line); };
    auto digitized = project_ops::digitize_image_into(out.project, request.image, settings, log);
    if (!digitized) {
        return std::unexpected(digitized.error());
    }
    out.report = std::move(*digitized);
    if (task.cancelled()) {
        return cancelled_error();
    }
    if (out.project.embroidery_objects.empty()) {
        return fail_coded(ErrorCategory::OperationImpossible, "E_NO_OBJECT",
                          "Aucun objet de broderie généré",
                          "Image trop uniforme ou régions trop petites : augmentez le nombre de "
                          "couleurs ou réduisez la taille minimale des régions.");
    }

    task.report(2, kDigitizeSteps);
    auto sequence = stitch_generation::effective_sequence(out.project, task);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    out.sequence = std::move(*sequence);
    out.stats = stitch::compute_stats(out.sequence);
    if (out.stats.stitches == 0) {
        return fail(ErrorCategory::OperationImpossible, "Aucun point généré");
    }
    out.width_mm = (out.stats.bounds.max.x.value - out.stats.bounds.min.x.value) / 1000.0;
    out.height_mm = (out.stats.bounds.max.y.value - out.stats.bounds.min.y.value) / 1000.0;
    task.report(kDigitizeSteps, kDigitizeSteps);
    return out;
}

Result<void> export_dst(const stitch::StitchSequence& sequence, const std::filesystem::path& path) {
    return formats::write_dst_file(path, sequence);
}

Result<stitch::StitchStats> export_project_dst(std::shared_ptr<const document::Project> project,
                                               const std::filesystem::path& path,
                                               const TaskContext& task) {
    if (!project) {
        return fail(ErrorCategory::Internal, "Export demandé sans projet");
    }
    auto sequence = stitch_generation::effective_sequence(*project, task);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    const auto stats = stitch::compute_stats(*sequence);
    if (stats.stitches == 0) {
        return fail(ErrorCategory::OperationImpossible, "Aucun point à exporter",
                    "Le document ne contient aucun objet brodé.");
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    if (auto written = export_dst(*sequence, path); !written) {
        return std::unexpected(written.error());
    }
    return stats;
}

} // namespace openstitch::desktop_v2
