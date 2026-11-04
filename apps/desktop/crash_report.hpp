// SPDX-License-Identifier: Apache-2.0
// Rapport de plantage (HP-QA-002) : capture d'un minidump et recherche des dumps récents.
// Sans Qt ; la capture est propre à Windows (MiniDumpWriteDump), no-op ailleurs.
#pragma once

#include <chrono>
#include <filesystem>
#include <optional>

namespace openstitch::desktop {

// Dossier des minidumps (`<dossier applicatif>/crash`). Vide si introuvable.
[[nodiscard]] std::filesystem::path crashDumpDirectory();

// Installe le filtre d'exceptions non gérées et `std::set_terminate` : à un crash, un fichier
// `crash-<horodatage>.dmp` est écrit dans `dir`, puis le processus se termine. Ne transmet
// rien nulle part. Renvoie false si la capture n'est pas disponible (hors Windows, dossier
// illisible). Idempotent.
bool installCrashHandler(const std::filesystem::path& dir);

// Dump le plus récent de `dir` dont l'âge est <= `maxAge` et qui n'a pas déjà été proposé
// (voir `markDumpSeen`). nullopt sinon.
[[nodiscard]] std::optional<std::filesystem::path>
findRecentUnseenDump(const std::filesystem::path& dir,
                     std::chrono::seconds maxAge = std::chrono::hours(24 * 7));

// Dernier dump du dossier, vu ou non (pour l'action de menu). nullopt si aucun.
[[nodiscard]] std::optional<std::filesystem::path> findLatestDump(const std::filesystem::path& dir);

// Marque le dump comme déjà proposé (fichier voisin `.seen`) : la proposition au démarrage
// ne se répète pas. Le dump reste en place.
void markDumpSeen(const std::filesystem::path& dump);

} // namespace openstitch::desktop
