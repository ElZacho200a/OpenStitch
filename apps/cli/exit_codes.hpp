// SPDX-License-Identifier: Apache-2.0
// Codes de sortie de openstitch-cli : source unique (HP-QA-018). Table publiée dans
// docs/source/cli-project.md (« Codes de sortie »). Les erreurs d'usage de la ligne de
// commande (option inconnue, valeur hors plage...) gardent les codes de CLI11.
#pragma once

namespace openstitch_cli {

// 0 : succès.
inline constexpr int kExitOk = 0;
// 1 : entrée ou E/S (fichier illisible, option incohérente, rien à produire), erreur interne.
inline constexpr int kExitInput = 1;
// 2 : contrôle qualité ou validation non satisfait.
inline constexpr int kExitQuality = 2;
// 3 : lot ou opération `project` refusé (aucune écriture).
inline constexpr int kExitRefused = 3;

} // namespace openstitch_cli
