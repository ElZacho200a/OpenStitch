// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <spdlog/spdlog.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace openstitch {

// Initialise le logger global (stderr couleur). À appeler une fois au
// démarrage de chaque exécutable.
void init_logging(bool verbose = false);

// Dossier applicatif utilisateur de l'application (HP-QA-002) : %LOCALAPPDATA%/OpenStitch
// sous Windows, $XDG_STATE_HOME (ou ~/.local/state)/openstitch ailleurs. Vide si introuvable.
[[nodiscard]] std::filesystem::path user_data_directory();

// Dossier du journal (`user_data_directory()/logs`).
[[nodiscard]] std::filesystem::path user_log_directory();

// Ajoute au logger global un journal fichier `openstitch.log` dans `dir` (créé au besoin),
// à rotation bornée : au plus `max_files` fichiers de `max_bytes` octets chacun. Renvoie
// false (sans lever) si le fichier est inutilisable ; le journal console reste actif.
// N'influence jamais les sorties de broderie.
bool init_file_logging(const std::filesystem::path& dir, std::size_t max_bytes = 1024 * 1024,
                       std::size_t max_files = 3);

// Dernières `max_lines` lignes du journal courant de `dir` (vide si absent).
[[nodiscard]] std::vector<std::string> read_log_tail(const std::filesystem::path& dir,
                                                     std::size_t max_lines);

} // namespace openstitch
