// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- diagnostic de brodabilité : exécute `project_ops::diagnose_project` (le MÊME
// contrôle que `openstitch-cli project diagnose` et que l'agent) sur un instantané immuable, hors
// fil d'interface. Le résultat porte la révision analysée : l'interface sait quand il est périmé.
// Sans Qt.

#include <cstdint>
#include <memory>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/analysis.hpp"

namespace openstitch::desktop_v2 {

struct DiagnosticsResult {
    std::vector<project_ops::Finding> findings;
    std::size_t errors{0};
    std::size_t warnings{0};
    std::size_t infos{0};
    double motifWidthMm{0.0};
    double motifHeightMm{0.0};
    std::size_t stitches{0};
    std::size_t jumps{0};
    std::size_t trims{0};
    std::size_t colorChanges{0};
    double threadM{0.0};
    std::uint64_t revision{0}; // révision du document analysée
};

[[nodiscard]] Result<DiagnosticsResult>
run_diagnostics(std::shared_ptr<const document::Project> project, std::uint64_t revision,
                const TaskContext& task);

} // namespace openstitch::desktop_v2
