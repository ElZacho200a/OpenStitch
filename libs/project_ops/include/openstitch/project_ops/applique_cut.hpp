// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::project_ops {

struct AppliqueCutReport {
    std::size_t objects{0}; // objets appliqués exportés
    std::size_t paths{0};   // contours écrits (extérieurs et trous)
};

// Exporte le contour de découpe du tissu des objets APPLIQUÉS du projet (HP-SPEC-001) : tous les
// appliqués visibles, ou seulement `only`. Le format suit l'extension de `path` : `.svg` (mm,
// découpe) ou `.dxf` (polylignes, mm). Écriture atomique. Refus si aucun appliqué n'est concerné
// ou si l'extension est inconnue. Ne modifie pas le projet.
[[nodiscard]] Result<AppliqueCutReport> export_applique_cut(const document::Project& project,
                                                            const std::filesystem::path& path,
                                                            std::optional<ObjectId> only = {});

} // namespace openstitch::project_ops
