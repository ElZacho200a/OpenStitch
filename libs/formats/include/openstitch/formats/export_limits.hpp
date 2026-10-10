// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "openstitch/formats/format_registry.hpp"
#include "openstitch/formats/machine_design.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::formats {

enum class ExportIssueSeverity : std::uint8_t { Info, Warning, Error };

struct ExportIssue {
    ExportIssueSeverity severity{ExportIssueSeverity::Info};
    std::string message;
};

// HP-FMT-002..005 : analyse PRÉ-EXPORT propre au format cible (pure, sans écrire) :
// nombre de couleurs vs limite du format, étendue vs plage codable, déplacements qui seront
// découpés en plusieurs enregistrements, cadres connus de la marque, couleurs perdues
// (EXP/DST). Un `Error` signifie que `encode` refuserait ce motif.
[[nodiscard]] std::vector<ExportIssue>
check_export_limits(const stitch::StitchSequence& sequence, const FormatInfo& format,
                    const MachineExportOptions& options = {});

} // namespace openstitch::formats
