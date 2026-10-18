// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/factory.hpp"

namespace openstitch::project_ops {

inline constexpr const char* kDigitizeReportSchema = "openstitch.digitize-report/1";
inline constexpr const char* kErrorSchema = "openstitch.error/1";
inline constexpr const char* kDiffSchema = "openstitch.project-diff/1";
inline constexpr const char* kSelectionSchema = "openstitch.selection/1";

// Rapport de numérisation lisible par un agent : réglages effectifs, décision sur le fond,
// objets créés par type (identifiants), points/sauts/coupes/changements de fil/dimensions
// (séquence EFFECTIVE), mesures de qualité de `stitch_analysis` (déplacements longs, points
// courts, surface non couverte, objets trop petits), avertissements du moteur. Déterministe.
[[nodiscard]] Result<std::string> digitize_report_json(const document::Project& project,
                                                       const DigitizeReport& report,
                                                       const DigitizeSettings& settings,
                                                       const std::string& imagePath,
                                                       const std::string& projectPath);

// Chaîne JSON échappée, avec guillemets (pour composer de petits documents sans lib JSON).
[[nodiscard]] std::string json_quote(std::string_view text);

// Lecture humaine d'un catalogue (`operations_catalog_json`) et d'un rapport qualité
// (`build_quality_report().json`) : une ligne par opération / par constat. Erreur nommée si le
// JSON n'a pas le schéma attendu.
[[nodiscard]] Result<std::string> operations_catalog_text(const std::string& catalog_json);
[[nodiscard]] Result<std::string> quality_report_text(const std::string& quality_json);

// Enveloppe d'erreur JSON (schéma `openstitch.error/1`) pour stdout en mode --json.
[[nodiscard]] std::string error_json(const std::string& command, const std::string& code,
                                     const std::string& message, const std::string& hint);

// Différences entre deux projets (initiative) : cadre, finitions, objets ajoutés/supprimés/
// modifiés (type, couleur, ordre, paramètres, visibilité, géométrie source), compteurs et
// séquence (points). Même projet : `identical == true`. Déterministe.
[[nodiscard]] Result<std::string> diff_projects_json(const document::Project& a,
                                                     const document::Project& b);

// Transforme `key=value` répétés en JSON d'opération `{"op":…, key:valeur…}` : valeur lue
// comme littéral JSON (nombre, booléen, tableau, objet) sinon comme chaîne. Erreur nommée si
// un argument n'est pas de la forme clé=valeur. Sert aux commandes individuelles
// (`project op`) : même chemin que les lots.
[[nodiscard]] Result<std::string> operation_json_from_args(std::string_view op,
                                                           const std::vector<std::string>& kv);

} // namespace openstitch::project_ops
