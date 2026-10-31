// SPDX-License-Identifier: Apache-2.0
// Commandes `project ...` de openstitch-cli : interface programmatique du moteur (création,
// numérisation, inspection, opérations, lots, contrôle qualité). Aucune logique métier ici :
// tout passe par `openstitch::project_ops`.
#pragma once

#include <CLI/CLI.hpp>

#include <optional>
#include <string>
#include <vector>

#include "openstitch/project_ops/factory.hpp"

namespace openstitch_cli {

// Réglages de numérisation et leur chaîne `--skip-background` (auto|yes|no).
struct DigitizeOptionValues {
    openstitch::project_ops::DigitizeSettings settings;
    std::string skipBackground{"auto"};
    std::string
        settingsPath; // `--settings` (HP-FILE-014), déjà développé par expand_settings_profile
};

// HP-FILE-014 : si `args` (arguments sans le nom du programme, dans l'ordre) contient
// `--settings profil.json` pour une commande de numérisation, ajoute à `args` les options
// équivalentes du profil (celles non données explicitement). Renvoie un code de sortie en cas
// de refus (message déjà affiché), `std::nullopt` sinon.
[[nodiscard]] std::optional<int> expand_settings_profile(CLI::App& app,
                                                         std::vector<std::string>& args);

// Déclare les options de numérisation communes à `digitize` et `project digitize`.
void add_digitize_options(CLI::App* cmd, DigitizeOptionValues& values);
// À appeler après le parse : traduit `skipBackground` en `settings.skip_background`.
void finalize_digitize_options(DigitizeOptionValues& values);

// Codes de sortie des commandes `project` (en plus de ceux de la CLI) : 0 succès ; 1 entrée,
// fichier ou erreur interne ; 2 contrôle qualité / validation non satisfait ; 3 lot ou
// opération refusé (aucune écriture).
inline constexpr int kExitRefused = 3;

// Enregistre le groupe `project` et `commands`. Renvoie le sous-commande `project`.
CLI::App* register_project_commands(CLI::App& app, bool& jsonOut);
CLI::App* register_commands_listing(CLI::App& app, bool& jsonOut);

// Code de sortie posé par le dernier callback exécuté.
[[nodiscard]] int project_exit_code();

} // namespace openstitch_cli
