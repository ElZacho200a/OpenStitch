// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "openstitch/core/error.hpp"
#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::thread_palette {

// Import d'un nuancier fourni par l'UTILISATEUR (HP-THR-002 : les cartes
// commerciales ne sont pas redistribuables avec l'application, l'utilisateur
// charge les siennes). Format CSV (UTF-8, séparateur `,` ou `;` détecté sur
// l'en-tête, guillemets doubles acceptés, lignes vides et lignes commençant
// par `#` ignorées) :
//
//   code,name,hex                  (hex = #RRGGBB ou RRGGBB)
//   code,name,r,g,b                (entiers 0..255)
//
// L'en-tête est OBLIGATOIRE (noms insensibles à la casse ; synonymes :
// `reference`/`ref` pour code, `nom` pour name, `marque` pour brand, `gamme`
// pour range). Colonnes facultatives : `brand`, `range`. Toute ligne
// invalide (code vide, couleur illisible, code en double) fait échouer
// l'import avec le numéro de ligne : jamais de nuancier à moitié lu.
struct ChartImportInfo {
    std::string chart_id;     // voir make_user_chart_id
    std::string display_name; // nom affiché
    std::string source_note;  // provenance (fichier, date...) pour la traçabilité
};

[[nodiscard]] Result<ThreadChart> parse_chart_csv(std::string_view text,
                                                  const ChartImportInfo& info);

// Identifiant stable d'un nuancier utilisateur : "user_" + nom normalisé
// (minuscules, [a-z0-9_]). Le préfixe évite toute collision avec les
// nuanciers intégrés.
[[nodiscard]] std::string make_user_chart_id(std::string_view name);

// "#RRGGBB" / "RRGGBB" -> rgb ; nullopt si invalide.
[[nodiscard]] std::optional<std::array<std::uint8_t, 3>> parse_hex_color(std::string_view text);

// rgb -> "#RRGGBB" (majuscules).
[[nodiscard]] std::string to_hex_color(std::array<std::uint8_t, 3> rgb);

} // namespace openstitch::thread_palette
