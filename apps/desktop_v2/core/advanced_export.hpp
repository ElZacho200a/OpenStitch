// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "openstitch/core/task_control.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_ops/color_registration.hpp"

namespace openstitch::desktop_v2 {

// Options propres au fichier exporté, comme en V1. Les réglages de génération restent ceux
// du document (export_settings, une commande annulable). Aucun point n'est persisté ici.
struct AdvancedExportOptions {
    formats::MachineExportOptions machine;
    project_io::ExportOptimizations routes;
    bool colorCompanion{false};
    formats::CompanionKind companionKind{formats::CompanionKind::Csv};
    project_ops::ColorRegistrationOptions registration;
};

struct AdvancedExportReport {
    stitch::StitchStats stats;
    std::string summary;
    std::filesystem::path companion;
};

// Même production que la CLI : export_sequence, codecs du registre, solveur de décalage.
// Tous les refus (cadre, champ, format, options) précèdent l'écriture atomique.
[[nodiscard]] Result<AdvancedExportReport>
writeAdvancedExport(std::shared_ptr<const document::Project> project,
                    const std::filesystem::path& path, const AdvancedExportOptions& options,
                    const TaskContext& task);

} // namespace openstitch::desktop_v2
