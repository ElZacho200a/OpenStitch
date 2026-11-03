// SPDX-License-Identifier: Apache-2.0
// Commandes `ai ...` de openstitch-cli : diagnostic des backends de vision IA déclarés
// (protocole openstitch.ai.backend/1). Aucune logique métier ici : tout passe par
// `openstitch::ai_segmentation`. Sans configuration, rien n'est lancé.
#pragma once

#include <CLI/CLI.hpp>

namespace openstitch_cli {

// Enregistre le groupe `ai` ; renvoie la sous-commande `ai`.
CLI::App* register_ai_commands(CLI::App& app);

// Code de sortie posé par le dernier callback `ai` exécuté.
[[nodiscard]] int ai_exit_code();

} // namespace openstitch_cli
