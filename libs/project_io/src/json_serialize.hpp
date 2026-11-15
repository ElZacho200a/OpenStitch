// SPDX-License-Identifier: Apache-2.0
// En-tête interne : conversion du document en JSON (hors pixels d'image et
// carte de segmentation, stockés en binaire dans l'archive).
#pragma once

#include <nlohmann/json.hpp>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::project_io::detail {

// Sérialise tout le document SAUF : original (pixels), segmentation.labels
// (binaire). Les métadonnées de segmentation (dimensions, régions) sont dans
// le JSON ; les labels sont rétablis séparément par project_io.
[[nodiscard]] nlohmann::json project_to_json(const document::Project& project);

// Reconstruit le document depuis le JSON (labels de segmentation à part).
[[nodiscard]] Result<document::Project> project_from_json(const nlohmann::json& j);

// Migrations de `migrate_project_json` appliquées directement à l'arbre déjà lu (même
// contrôle de version, mêmes erreurs) : évite de relire et de réécrire le texte JSON.
[[nodiscard]] Result<void> migrate_project_json_in_place(nlohmann::json& root);

} // namespace openstitch::project_io::detail
