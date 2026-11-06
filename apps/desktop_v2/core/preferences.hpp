// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- préférences d'interface hors document : thème et densité. Un petit fichier texte
// `clé=valeur` (une clé par ligne), relu sans jamais échouer : une valeur absente ou invalide
// retombe sur le défaut de CETTE clé seulement. Sans Qt ; l'interface applique les valeurs.
//
// Langue : non traitée. L'interface n'a qu'une langue (le français, texte source) et aucune
// infrastructure de traduction ; proposer un choix de langue serait un faux réglage.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "render_settings.hpp"

namespace openstitch::desktop_v2 {

enum class UiDensity { Compact, Normal, Spacious };

struct Preferences {
    std::string theme{"system"}; // "system" | "light" | "dark"
    UiDensity density{UiDensity::Normal};
    RenderSettings render; // qualité du rendu réaliste de l'aperçu

    bool operator==(const Preferences&) const = default;
};

// Facteur appliqué aux marges intérieures de la feuille de style : 0,8 / 1,0 / 1,3.
[[nodiscard]] double densityScale(UiDensity density);
[[nodiscard]] std::string densityId(UiDensity density); // "compact" | "normal" | "spacious"
[[nodiscard]] std::optional<UiDensity> parseDensity(std::string_view id);
[[nodiscard]] std::string densityLabel(UiDensity density); // libellé français

[[nodiscard]] std::string preferencesToText(const Preferences& preferences);
// Texte inconnu, vide ou partiellement invalide : défauts clé par clé, jamais d'échec.
[[nodiscard]] Preferences preferencesFromText(std::string_view text);

[[nodiscard]] bool savePreferences(const std::filesystem::path& file,
                                   const Preferences& preferences);
// Fichier absent ou illisible : préférences par défaut.
[[nodiscard]] Preferences loadPreferences(const std::filesystem::path& file);

} // namespace openstitch::desktop_v2
