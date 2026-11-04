// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- catalogue de commandes pour la palette (Ctrl+K) : un identifiant stable par
// intention (outil, espace, fichier, édition, sélection…), filtrage insensible à la casse et aux
// accents. Sans Qt ; l'interface construit les entrées et exécute l'identifiant choisi (principe C
// : une intention, un chemin).

#include <string>
#include <string_view>
#include <vector>

namespace openstitch::desktop_v2 {

struct CommandEntry {
    std::string id;       // « tool.draw.ellipse », « workspace.digitize », « file.open »…
    std::string label;    // libellé affiché (français = langue source)
    std::string group;    // « Outil », « Espace », « Fichier »…
    std::string shortcut; // affichage seulement
    bool enabled{true};   // faux : affiché grisé avec sa raison, non exécutable
};

// « Éléphant » -> « elephant » : minuscules + accents latins courants retirés (UTF-8).
[[nodiscard]] std::string normalizeForSearch(std::string_view text);

// Toutes les entrées dont libellé, groupe ou identifiant contiennent CHAQUE mot de la requête.
// Ordre : libellé commençant par un mot > libellé contenant > reste ; activées avant désactivées ;
// à égalité, ordre d'origine (stable, donc déterministe). Requête vide : toutes, activées d'abord.
[[nodiscard]] std::vector<const CommandEntry*>
filterCommands(const std::vector<CommandEntry>& entries, std::string_view query);

} // namespace openstitch::desktop_v2
