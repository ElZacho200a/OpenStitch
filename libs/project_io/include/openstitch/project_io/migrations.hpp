// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "openstitch/core/error.hpp"

namespace openstitch::project_io {

// Une étape de migration du schéma .osp : transforme le JSON d'une version `from` en JSON
// de la version `from + 1` (HP-FMT-021). Les étapes sont enregistrées dans une table
// ordonnée et contiguë ; un fichier de version N est relu en appliquant, dans l'ordre,
// les étapes N, N+1, ... jusqu'à la plus récente version lisible.
struct MigrationStepInfo {
    int from = 0;
    int to = 0;
    std::string description;
};

// Table des étapes enregistrées (ordre croissant, sans trou : de 1 à kMaxReadableSchemaVersion-1).
[[nodiscard]] std::vector<MigrationStepInfo> migration_steps();

// Applique les migrations au texte JSON d'un `project.json` (enveloppe complète :
// `schemaVersion` + `document`). Retourne le JSON migré, dont `schemaVersion` vaut la plus
// récente version lisible. Sortie déterministe (clés triées par nlohmann). Erreurs :
// JSON illisible ou version invalide -> InvalidFile ; version plus récente que celle que
// cette version sait lire -> UnsupportedFormat, avec un message clair.
[[nodiscard]] Result<std::string> migrate_project_json(std::string_view jsonText);

// Version de schéma déclarée par un `project.json` (0 si absente ou JSON illisible).
[[nodiscard]] int declared_schema_version(std::string_view jsonText);

} // namespace openstitch::project_io
