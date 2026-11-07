// SPDX-License-Identifier: Apache-2.0
#include "advanced_export.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "export_settings.hpp"
#include "openstitch/core/atomic_file.hpp"
#include "openstitch/document/machine_profile.hpp"
#include "openstitch/formats/export_limits.hpp"
#include "openstitch/stitch_analysis/color_blocks.hpp"

namespace openstitch::desktop_v2 {

Result<AdvancedExportReport> writeAdvancedExport(std::shared_ptr<const document::Project> project,
                                                 const std::filesystem::path& path,
                                                 const AdvancedExportOptions& options,
                                                 const TaskContext& task) {
    if (!project) {
        return fail(ErrorCategory::Internal, "Export demandé sans projet.");
    }
    auto ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!ext.empty() && ext.front() == '.')
        ext.erase(0, 1);
    const auto* format = formats::find_format_for_extension(ext);
    if (!format || !format->can_write || (!format->encode && !format->encode_ex)) {
        return fail(ErrorCategory::UnsupportedFormat, "Format d'export inconnu : ." + ext);
    }
    if (options.registration.compensate && format->id != "dst") {
        return fail(ErrorCategory::UserInput, "La compensation du décalage s'exporte en DST.");
    }
    if (!std::isfinite(options.registration.strength) || options.registration.strength < 0 ||
        options.registration.strength > 1 || options.registration.iterations < 1 ||
        options.registration.iterations > 10 ||
        options.routes.hidden_travel.max_bridge_length.value < 0 ||
        options.routes.hidden_travel.max_bridge_length.value > 100'000) {
        return fail(ErrorCategory::UserInput, "Réglages d'export hors limites.");
    }
    if (task.cancelled())
        return cancelled_error();
    auto finished = project_io::export_sequence(*project, options.routes);
    if (!finished)
        return std::unexpected(finished.error());
    auto sequence = std::move(finished->sequence);
    auto machine = options.machine;
    std::vector<formats::ThreadColor> colors;
    const auto blocks = stitch_analysis::color_blocks(*project, sequence);
    for (const auto& block : blocks)
        colors.push_back(block.rgb);
    if (machine.block_colors.empty())
        machine.block_colors.assign(colors.begin(), colors.end());
    if (options.registration.compensate) {
        const auto prediction = project_ops::predict_color_registration_sequence(
            *project, sequence, options.registration);
        if (!prediction)
            return std::unexpected(prediction.error());
        sequence = prediction->compensated;
    }
    auto check = checkExportSequence(*project, sequence, format->id);
    if (!check.ok || !check.fitsHoop || !check.fitsMachineField || !check.formatAccepted) {
        return fail(ErrorCategory::OperationImpossible, check.summary());
    }
    sequence = project_io::with_start_point(std::move(sequence), machine.start);
    for (const auto& issue : formats::check_export_limits(sequence, *format, machine)) {
        if (issue.severity == formats::ExportIssueSeverity::Error) {
            return fail(ErrorCategory::OperationImpossible, issue.message);
        }
    }
    const auto bytes =
        format->encode_ex ? format->encode_ex(sequence, machine) : format->encode(sequence);
    if (!bytes)
        return std::unexpected(bytes.error());
    if (task.cancelled())
        return cancelled_error();
    if (const auto wrote = write_file_atomic(path, std::span<const std::uint8_t>(*bytes)); !wrote) {
        return std::unexpected(wrote.error());
    }
    AdvancedExportReport report{stitch::compute_stats(sequence), check.summary(), {}};
    if (options.registration.compensate) {
        report.summary += " Compensation indicative non calibrée : essai sur tissu nécessaire.";
    }
    if (options.colorCompanion && !format->carries_colors) {
        const auto companion =
            formats::write_color_companion_file(path, colors, options.companionKind);
        if (!companion) {
            report.summary += " Fichier machine écrit ; fichier de couleurs refusé : " +
                              companion.error().message;
        } else {
            report.companion = formats::companion_path(path, options.companionKind);
        }
    }
    return report;
}

} // namespace openstitch::desktop_v2
