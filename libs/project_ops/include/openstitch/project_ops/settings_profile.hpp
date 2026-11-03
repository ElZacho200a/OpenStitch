// SPDX-License-Identifier: Apache-2.0
// HP-FILE-014 : profil de numérisation (`--settings profil.json`) pour la CLI. Le profil est un
// objet JSON dont les clés sont les noms longs des options de numérisation, sans « -- »
// (`{"max-colors": 6, "dark-lines": true, "hidden-travel-levels": ["between","within"]}`).
// Il est traduit en arguments de ligne de commande équivalents : le résultat est donc
// identique, octet pour octet, à celui des options passées à la main, et les bornes sont
// celles de la CLI.
#pragma once

#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/core/error.hpp"

namespace openstitch::project_ops {

inline constexpr const char* kSettingsProfileSchemaHint =
    "objet JSON {\"nom-d-option\": valeur} ; un booléen pour un drapeau, un nombre ou une chaîne "
    "sinon (liste de chaînes pour --hidden-travel-levels)";

struct ProfileKey {
    std::string name; // nom long sans « -- »
    bool flag{false}; // option sans valeur
};

// Traduit `json_text` en arguments (`--nom`, `valeur`), dans l'ordre alphabétique des clés.
// Une clé déjà donnée explicitement sur la ligne de commande (`explicit_names`) est ignorée :
// la ligne de commande l'emporte sur le profil. Une clé inconnue, un type inattendu ou un JSON
// invalide est refusé avec un `detail` (piste de correction).
[[nodiscard]] Result<std::vector<std::string>>
settings_profile_args(std::string_view json_text, std::span<const ProfileKey> known,
                      const std::set<std::string>& explicit_names);

} // namespace openstitch::project_ops
