// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>

#include "openstitch/core/error.hpp"
#include "openstitch/document/imported_design.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::project_io {

// AD-03/AI-03b : composition « projet -> fichier machine » / « fichier
// machine -> design importé », réutilisée à l'identique par le desktop
// (menus Importer/Exporter) et la CLI -- la SEULE logique de ce genre,
// jamais dupliquée. `formats` reste sans dépendance sur `document` (AD-02) ;
// cette composition est ce qui les relie. Générique sur
// `formats::registered_formats()` : S2b/S2c (PES, JEF, EXP) n'ont besoin
// d'aucune modification ici, seulement d'une ligne de registre (cf.
// `libs/formats/src/format_registry.cpp`).
//
// `format_id` nomme une entrée du registre ; "dst" est la seule disponible
// tant que S2b/S2c ne sont pas faits.

// Exporte `project` (sa séquence EFFECTIVE -- retouches manuelles incluses,
// cf. `stitch_generation::effective_sequence`, jamais `generate_sequence`
// directement) vers un fichier du format `format_id`, à `path`.
[[nodiscard]] Result<void> export_machine_file(const document::Project& project,
                                               const std::string& format_id,
                                               const std::filesystem::path& path);

// Importe un fichier machine comme design importé (AD-04, AI-04) --
// construit la valeur à placer dans `Project::imported_design` (l'appelant
// décide, via une commande, si/comment elle entre dans un document). Si
// `format_id` est vide, déduit du registre à partir de l'extension de
// `path` (minuscule, sans le point).
[[nodiscard]] Result<document::ImportedDesign>
import_machine_file(const std::filesystem::path& path, std::string format_id = {});

} // namespace openstitch::project_io
