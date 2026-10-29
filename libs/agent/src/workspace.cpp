// SPDX-License-Identifier: Apache-2.0
// Espace de travail Claude Code généré par OpenStitch : serveur MCP --live, consignes injectées par
// un hook SessionStart et skills. Aucune exécution ici : uniquement des fichiers.
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "openstitch/agent/agent.hpp"

namespace openstitch::agent {

namespace fs = std::filesystem;

namespace {

constexpr const char* kGuide = R"(# OpenStitch Studio — mode live (consignes pour Claude Code)

Tu travailles avec le projet de broderie OUVERT dans OpenStitch Studio, via le serveur MCP
« openstitch » (mode --live). Le projet peut être non enregistré : l'état visible dans le logiciel
est l'état à modifier. Réponds en français, de façon concise.

## Au démarrage : analyse sémantique
Dès que tu es lancé sur un projet non vide, avant toute autre demande : (1) `live_context` ;
(2) `project_render` pour la vue d'ensemble ; (3) `live_object_views` par lots de 8 objets
(regroupe d'abord par fil avec `thread_palette`) ; (4) dresse la table « objet → ce qu'il
REPRÉSENTE » (ex. pétale, feuille, nénuphar, contour, ombre) avec ton niveau de confiance ;
(5) PROPOSE (sans l'appliquer) d'inscrire ces noms dans le projet via des opérations `rename`
(`live_preview` puis `project_apply`, approbation dans le dock). Ne modifie rien sans approbation.
Résume ensuite le motif en deux phrases.

## Cycle de travail
1. `live_context` (ou `live_status`) : révision, sélection, palette des FILS, objets, risques.
2. `live_target` : cible par teinte de fil (« orange », « #RRGGBB », « selection »). Si
   `ambiguous` est vrai, pose la question avec `live_ask_user` (l'utilisateur répond dans le panneau
   Compagnon IA) AVANT toute modification.
3. `live_plan_satin` : compare skeleton / parallel_sweep / medial_axis, justifie les refus (le
   tatami est conservé), prépare un lot validé en simulation.
4. `live_preview` : avant/après (points, sauts, constats, risque textile, rendu) sur une copie.
5. `project_apply` : applique le lot. L'utilisateur approuve dans le dock ; un seul Ctrl+Z annule.
6. Contrôle : `project_check`, `project_render`. Au plus 3 itérations par demande.

## Règles
- Les outils de lecture ne modifient jamais le projet. Seul `project_apply` modifie, après
  approbation. `E_STALE_REVISION` : relis `live_context` puis recommence. `E_SESSION_CLOSED` :
  OpenStitch est fermé ou le pont est inactif (menu Aide ▸ Pont IA local).
- Les couleurs sont celles des FILS cousus, pas les teintes de segmentation de l'image.
- `Predicted` est une compensation de tirage EMPIRIQUE, non calibrée sur machine : dis-le. Ne promets
  jamais de convertir toute forme en satin.
- Tu ne peux ni ouvrir, ni enregistrer, ni exporter de fichier via OpenStitch : l'utilisateur le fait
  dans le logiciel.
)";

constexpr const char* kSkillEdit = R"(---
name: openstitch-live-edit
description: Modifier le projet OpenStitch ouvert (couleurs, types de points, paramètres) via le pont live avec aperçu et approbation. À utiliser pour toute demande de modification du projet ouvert.
---

# Modifier le projet ouvert

1. `live_context` puis `live_target` pour identifier les objets (identifiants stables).
2. `project_operations` pour le catalogue des opérations ; construis un lot `project_apply`.
3. `live_preview` avec ce lot : montre l'avant/après à l'utilisateur.
4. `project_apply` (sans dry_run) : l'approbation se fait dans le dock ; annulable en un Ctrl+Z.
5. `project_check` et `project_render` pour vérifier. Si l'utilisateur rejette, ne réessaie pas
   le même lot : demande ce qui ne convient pas.
)";

constexpr const char* kSkillSemantic = R"(---
name: openstitch-live-semantic
description: Analyse sémantique du projet OpenStitch ouvert - identifier à quoi correspond chaque objet de broderie (pétale, feuille, contour…) à partir de vues zoomées, puis proposer de les nommer. À utiliser au démarrage sur un projet non vide.
---

# Analyse sémantique du motif

1. `live_context` : objets, fils, familles de couleur, sélection.
2. `project_render` (vue d'ensemble) pour comprendre le sujet global.
3. `live_object_views` par lots de 8 objets, en commençant par les plus grands / les plus
   nombreux par fil : regarde chaque image et nomme ce que l'objet représente.
4. Table finale : identifiant, nom proposé, confiance (haute/moyenne/basse), fil. Les objets
   douteux sont listés à part, jamais inventés.
5. Propose des `rename` ; `live_preview` puis `project_apply` seulement après accord.
)";

constexpr const char* kSkillSatin = R"(---
name: openstitch-live-satin
description: Convertir des formes en satin avec compensation de tirage Predicted sur le projet OpenStitch ouvert (ciblage par fil, comparaison des 3 stratégies, refus justifiés). À utiliser pour « mets ces formes en satin ».
---

# Passer des formes en satin

1. `live_target` avec la teinte demandée ; si `ambiguous`, `live_ask_user`.
2. `live_plan_satin` sur les candidats (20 au plus par appel) : lis `targets[].verdict` ; les
   `refused` restent en tatami, explique les motifs ; `skipped` = verrouillé, masqué ou déjà satin.
3. `live_preview` sur `batch.operations`, présente points/sauts/constats/risque textile.
4. `project_apply` avec le même lot. Rappelle que `Predicted` est empirique et non calibré machine :
   un essai sur tissu reste nécessaire.
)";

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

} // namespace

bool write_claude_workspace(const std::string& directory, const std::string& mcp_exe_path,
                            int desktop_pid) {
    using json = nlohmann::json;
    try {
        // Les chemins fournis par Qt sont en UTF-8, y compris les repertoires Windows accents.
        const fs::path dir = fs::path(
            std::u8string(reinterpret_cast<const char8_t*>(directory.data()), directory.size()));
        write_file(dir / "CLAUDE.md", kGuide);
        write_file(dir / ".claude" / "openstitch-live-guide.md", kGuide);
        write_file(dir / ".claude" / "skills" / "openstitch-live-edit" / "SKILL.md", kSkillEdit);
        write_file(dir / ".claude" / "skills" / "openstitch-live-satin" / "SKILL.md", kSkillSatin);
        write_file(dir / ".claude" / "skills" / "openstitch-live-semantic" / "SKILL.md",
                   kSkillSemantic);
        json args = json::array({"--live"});
        if (desktop_pid > 0) {
            args.push_back("--pid");
            args.push_back(std::to_string(desktop_pid));
        }
        const json mcp{
            {"mcpServers", {{"openstitch", {{"command", mcp_exe_path}, {"args", args}}}}}};
        write_file(dir / ".mcp.json", mcp.dump(2));
        // Hook SessionStart : réinjecte les consignes à chaque démarrage / reprise de session.
        // Claude Code exécute les hooks via un shell POSIX (Git Bash sous Windows) : `cat` partout.
        const std::string command = "cat \"$CLAUDE_PROJECT_DIR/.claude/openstitch-live-guide.md\"";
        const json settings{
            {"hooks",
             {{"SessionStart", json::array({{{"hooks", json::array({{{"type", "command"},
                                                                     {"command", command}}})}}})}}},
            {"enabledMcpjsonServers", json::array({"openstitch"})}};
        write_file(dir / ".claude" / "settings.json", settings.dump(2));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace openstitch::agent
