// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string_view>

#include "openstitch/core/error.hpp"
#include "openstitch/thread_palette/chart_import.hpp"
#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::project_io {

// Import de nuanciers utilisateur depuis un fichier (HP-THR-002 : les cartes
// de marques ne sont pas redistribuables, l'utilisateur charge les siennes).
//
// Format JSON (UTF-8) :
//   {
//     "name":   "Mes fils",                      // nom affiché (défaut : nom du fichier)
//     "source": "carte papier 2024",             // provenance (facultatif)
//     "threads": [
//       { "code": "1001", "name": "Rouge vif",
//         "rgb": "#C8102E" ou [200, 16, 46],     // hexadécimal ou tableau r,g,b
//         "brand": "Acme", "range": "Poly 40" }  // facultatifs
//     ]
//   }
// Format CSV : voir thread_palette/chart_import.hpp. Dans les deux cas
// l'identifiant du nuancier est dérivé du nom (préfixe « user_ »,
// `make_user_chart_id`), jamais lu dans le fichier : un fichier ne peut pas
// se faire passer pour un nuancier intégré.

[[nodiscard]] Result<thread_palette::ThreadChart>
parse_thread_chart_json(std::string_view text, const thread_palette::ChartImportInfo& defaults);

// Lit un fichier `.csv` ou `.json` (selon l'extension, insensible à la casse).
// `source_note` par défaut : « Importé depuis <nom du fichier> ».
[[nodiscard]] Result<thread_palette::ThreadChart>
read_thread_chart_file(const std::filesystem::path& path);

} // namespace openstitch::project_io
