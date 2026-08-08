// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>

namespace openstitch::desktop {

// Conversion de chemins Windows <-> WSL, en un seul endroit (jamais de
// concaténation de chaînes ad-hoc ailleurs dans le code). Hypothèse MVP :
// le dossier des modèles et les dossiers de tâche IA vivent sur un disque
// Windows monté (`/mnt/<lettre>/...` côté WSL) — pas dans le système de
// fichiers Linux pur, pour rester lisible des deux côtés sans passer par
// `\\wsl$\...`.
class WslPathConverter {
public:
    // "C:\Users\foo\bar" -> "/mnt/c/Users/foo/bar". Une entrée déjà au
    // format WSL (commence par '/') est renvoyée telle quelle.
    [[nodiscard]] static QString toWsl(const QString& windowsPath);

    // "/mnt/c/Users/foo/bar" -> "C:\Users\foo\bar". Renvoie l'entrée telle
    // quelle si elle ne suit pas le schéma /mnt/<lettre>/....
    [[nodiscard]] static QString toWindows(const QString& wslPath);
};

}  // namespace openstitch::desktop
