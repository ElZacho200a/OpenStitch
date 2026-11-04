// SPDX-License-Identifier: Apache-2.0
#include "diagnostics_service.hpp"

namespace openstitch::desktop_v2 {

Result<DiagnosticsResult> run_diagnostics(std::shared_ptr<const document::Project> project,
                                          std::uint64_t revision, const TaskContext& task) {
    if (!project) {
        return fail(ErrorCategory::Internal, "Diagnostic demandé sans projet");
    }
    if (project->embroidery_objects.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Rien à analyser",
                    "Le document ne contient aucun objet brodé.");
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    auto report = project_ops::diagnose_project(*project);
    if (!report) {
        return std::unexpected(report.error());
    }
    if (task.cancelled()) {
        return cancelled_error(); // analyse terminée mais périmée : jamais livrée
    }
    DiagnosticsResult out;
    out.findings = std::move(report->findings);
    out.errors = report->errors;
    out.warnings = report->warnings;
    out.infos = report->infos;
    out.motifWidthMm = report->motif_width_mm;
    out.motifHeightMm = report->motif_height_mm;
    out.stitches = report->stitches;
    out.jumps = report->jumps;
    out.trims = report->trims;
    out.colorChanges = report->color_changes;
    out.threadM = report->thread_m;
    out.revision = revision;
    return out;
}

} // namespace openstitch::desktop_v2
