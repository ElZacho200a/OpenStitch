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
(2) `project_render` pour la vue d'ensemble ; (3) `live_object_views` par lots de 8 objets, AU PLUS 5 lots (les plus grands, un échantillon par fil ; déduis le reste par fil et voisinage)
(regroupe d'abord par fil avec `thread_palette`) ; (4) dresse la table « objet → ce qu'il
REPRÉSENTE » (ex. pétale, feuille, nénuphar, contour, ombre) avec ton niveau de confiance ;
(5) PROPOSE (sans l'appliquer) d'inscrire ces noms dans le projet via des opérations `rename`
(`live_preview` puis `project_apply`, approbation dans le dock). Ne modifie rien sans approbation.
Résume ensuite le motif en deux phrases.

## Skills de numérisation
Les skills `openstitch-digitize` (méthode complète + références : segmentation sémantique, choix
satin/tatami/contour, retouches et validation, diagnostic, paramètres) sont dans
`.claude/skills/`. Utilise-les pour RAISONNER (comprendre le motif, choisir satin ou tatami,
corriger). Dans ce mode live, `project_digitize`, `project_open`, `project_save` et
`project_export` ne sont PAS disponibles : l'utilisateur numérise, ouvre et enregistre depuis
OpenStitch ; toi, tu analyses et modifies le projet ouvert avec les outils `live_*` et
`project_apply`. `digitize_guide` renvoie aussi ces documents par l'outil MCP.
## Skills disponibles (.claude/skills/)
`openstitch-live-edit`, `-satin`, `-semantic`, `-diagnose`, `-fabric`, `-order`, `-cleanup`, `-naming`,
`-review`, `-lettering`, `-layout`, `-variants`, et `openstitch-digitize` (méthode de numérisation). Charge celui qui correspond à la demande.

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
3. `live_object_views` par lots de 8 objets, AU PLUS 5 lots (limite de tours), en commençant par les plus grands / les plus
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

constexpr const char* kSkillDiagnose = R"(---
name: openstitch-live-diagnose
description: Auditer la qualité du projet OpenStitch ouvert (points trop courts/longs, sauts, coupes, hors cadre, surépaisseurs, risque textile, fidélité à l'image) et proposer des corrections priorisées. À utiliser pour « le projet est-il propre ? », « qu'est-ce qui cloche ? », « vérifie avant d'exporter ».
---

# Audit qualité du projet ouvert

1. `project_report` : statut explicite par analyse (pass / warning / error / not_run / unsupported).
   Ne commente jamais une analyse `not_run` ou `unsupported` comme si elle avait été faite.
2. `project_diagnose` : constats détaillés avec objet et suggestion ; `project_check` pour le détail
   par objet ; `project_density` (couches, surépaisseurs) ; `project_fidelity` (trous intérieurs,
   détail perdu — nécessite l'image source).
3. `project_fabric_risk` : demande d'abord la matière (woven / jersey / felt). Le score est
   heuristique et NON calibré : ne le présente pas comme une preuve de brodabilité.
4. Classe les problèmes : bloquants (erreurs) > risques (surépaisseurs, risque textile) >
   confort (sauts, coupes). Donne les identifiants d'objets concernés (liens de focus dans le dock).
5. Pour chaque correction : lot d'opérations (`project_operations`), `live_preview` (écart de
   points/sauts/coupes/qualité), puis `project_apply` seulement après accord. Re-vérifie ensuite.
)";

constexpr const char* kSkillFabric = R"(---
name: openstitch-live-fabric
description: Adapter le projet OpenStitch ouvert à la matière (tissu tissé, jersey, feutre) - compensation de tirage (Predicted/Adaptive), densité, risque textile. À utiliser pour « ça va froncer ? », « adapte pour du jersey », « règle la compensation de tirage ».
---

# Adapter à la matière

1. Demande la matière (woven / jersey / felt) et si un stabilisateur est prévu.
2. `project_fabric_risk` avec `fabric` et `heatmap=true` : repère les zones chaudes ; `project_density`
   pour les surépaisseurs. Rappelle que le score est heuristique, non calibré machine.
3. Leviers (via `set_params`, champs visibles avec `project_inspect object=ID` et
   `project_operations`) : `pull_mode` (`manual` / `auto` / `adaptive` / `predicted`),
   `pull_compensation_mm`, espacement de rangée (densité). `Predicted` est EMPIRIQUE (PR #170) ;
   la calibration physique n'est pas faite : un essai sur tissu reste nécessaire.
4. Cible d'abord les grandes surfaces et les satins larges ; ne touche pas aux objets verrouillés.
5. `live_preview` (avant/après : points, sauts, risque textile), puis `project_apply` après accord.
)";

constexpr const char* kSkillOrder = R"(---
name: openstitch-live-order
description: Optimiser l'ordre de couture du projet OpenStitch ouvert pour réduire changements de fil, sauts et coupes, sans casser les recouvrements. À utiliser pour « réduis les coupes », « optimise l'ordre », « trop de changements de couleur ».
---

# Ordre de couture

1. `project_palette` : blocs de couleur dans l'ordre actuel ; `project_report`/`project_check` pour
   sauts et coupes de départ.
2. Opération `optimize_order` (stratégie et `improve` : voir `project_operations`) : les objets
   verrouillés ne bougent pas. Pour un cas précis : `set_order` (position, avant/après, début/fin).
3. Règle métier : le fond se coud avant les détails ; un objet recouvert ne passe pas après ce qui le
   recouvre. En cas de doute, ne déplace pas et explique pourquoi.
4. `live_preview` : compare sauts, coupes et changements de fil avant/après ; n'annonce un gain que
   mesuré. `project_apply` après accord (un seul Ctrl+Z annule).
)";

constexpr const char* kSkillCleanup = R"(---
name: openstitch-live-cleanup
description: Nettoyer le projet OpenStitch ouvert - îlots minuscules, trous parasites, vides entre zones, formes à fusionner ou séparer. À utiliser pour « enlève les petits îlots », « bouche les trous », « fusionne ces formes ».
---

# Nettoyage géométrique

1. Repère : `project_select` avec `area_lt=<mm²>` (petits objets), `project_locate` (qui a cousu
   autour d'un point), `project_diagnose` pour les suggestions.
2. Montre la liste (identifiants, aire, fil) à l'utilisateur AVANT toute suppression : un petit objet
   peut être un détail voulu (œil, point lumineux). Demande en cas de doute (`live_ask_user`).
3. Opérations : `remove_holes` (`max_area_mm2`), `fill_block_holes` (`closing_gap_mm`,
   `max_area_mm2`), `merge` (modes union/soustraction/intersection/exclusion), `break_apart`,
   `remove_object` en dernier recours.
4. `live_preview` puis `project_apply` après accord ; rappelle qu'un Ctrl+Z annule le lot.
)";

constexpr const char* kSkillNaming = R"(---
name: openstitch-live-naming
description: Nommer et regrouper les objets du projet OpenStitch ouvert selon ce qu'ils représentent (pétale, feuille, contour…) à partir de l'analyse sémantique. À utiliser après l'analyse sémantique ou pour « range le projet ».
---

# Nommer et regrouper

1. Pars de l'analyse (`openstitch-live-semantic`) : table identifiant → sens → confiance.
2. Convention de noms : « <sujet> <partie> <n> » (ex. « nénuphar pétale 3 »). Pas de nom pour les objets
   à faible confiance : liste-les à part.
3. Opérations : `rename` par objet, puis `group` (`objects`, `name`) par ensemble cohérent. Le
   groupement ne change pas l'ordre de couture.
4. `live_preview` puis `project_apply` après accord ; un seul Ctrl+Z défait tous les renommages.
)";

constexpr const char* kSkillReview = R"(---
name: openstitch-live-review
description: Donner un avis chiffré et honnête sur le projet OpenStitch ouvert (fidélité, propreté, brodabilité, efficacité) et suivre l'effet des modifications. À utiliser pour « note ce projet », « est-ce mieux qu'avant ? », bilan avant export.
---

# Avis chiffré

1. `project_report` (+ `project_fidelity` si l'image source est embarquée) : n'invente aucune mesure ;
   une analyse indisponible est dite indisponible.
2. Grille /20 : fidélité à l'image (6), propreté des constats (5), brodabilité/densités (5),
   efficacité sauts-coupes-changements (4). Justifie chaque note avec une mesure ou un constat.
3. Dis clairement ce que l'outil ne prouve pas : 0 erreur ≠ brodable ; densités simulées ;
   `Predicted` et le risque textile non calibrés machine ; un essai physique reste nécessaire.
4. Après modifications : compare aux mesures de départ (`live_preview` donne les écarts) ; cite les
   points de reprise (chaque application = une étape Ctrl+Z).
)";

constexpr const char* kSkillLettering = R"(---
name: openstitch-live-lettering
description: Ajouter, modifier ou supprimer du texte brodé (lettrage) dans le projet OpenStitch ouvert - taille, espacement, alignement, couleur, remplissage satin/tatami, lisibilité. À utiliser pour « ajoute le mot… », « change le texte », « agrandis les lettres ».
---

# Lettrage

1. `live_context` : cadre (mm), objets existants, fils déjà utilisés (réutilise une couleur du motif
   si l'utilisateur n'en précise pas). Demande le texte exact et l'emplacement s'ils manquent.
2. Opérations : `add_text` (`text`, `cap_height_mm`, `letter_spacing_mm`, `align`, `origin_mm`, `rgb`,
   `fill`, `density_mm`, `max_satin_width_mm`…), `set_text` (mise à jour partielle d'un texte existant,
   argument `text_object`), `remove_text`. Une lettre = un objet vectoriel + un objet de broderie.
3. Lisibilité : en dessous d'environ 5 mm de hauteur de capitale, le satin est fragile ; propose une
   taille minimale plutôt que de produire un texte illisible. Vérifie que le texte tient dans le cadre
   (`live_context` donne `canvas_mm`) et ne recouvre pas un détail important (`project_locate`).
4. `live_preview` : regarde le rendu et les écarts ; `project_apply` après accord. Un seul Ctrl+Z
   annule tout le lettrage.
)";

constexpr const char* kSkillLayout = R"(---
name: openstitch-live-layout
description: Mise en page du projet OpenStitch ouvert - cadre et marge de sécurité, déplacer / mettre à l'échelle / pivoter / retourner / dupliquer des objets, placement numérique, finitions (coupes, points d'arrêt). À utiliser pour « recentre », « agrandis », « fais tenir dans le cadre ».
---

# Mise en page et finitions

1. `live_context` : `canvas_mm`, objets, sélection. `project_check` signale les objets hors cadre.
2. Opérations : `set_canvas` (`width_mm`, `height_mm`, `margin_mm`, `hoop`), `place` (centre, taille
   et rotation numériques d'une sélection), `translate`, `scale` (uniforme), `rotate`, `mirror`,
   `duplicate_object`. Les objets verrouillés ne se déplacent pas sans demande explicite.
3. Agrandir ou réduire change densités et longueurs de point : après `scale`, relance
   `project_check` et `project_density`, et signale ce qui sort des limites.
4. Finitions : `set_finishing` (coupes, points d'arrêt, filtrage des points courts, sauts cachés) ;
   explique l'effet attendu et mesure-le avec `live_preview` (sauts, coupes).
5. `project_apply` après accord ; un seul Ctrl+Z annule le lot.
)";

constexpr const char* kSkillVariants = R"(---
name: openstitch-live-variants
description: Comparer plusieurs variantes d'une modification du projet OpenStitch ouvert (stratégies de satin, densités, ordres de couture) avant de choisir. À utiliser pour « essaie deux options », « quelle version est meilleure ? ».
---

# Comparer des variantes

1. Formule 2 à 3 variantes réalistes (lots d'opérations différents) sur les MÊMES objets.
2. Pour chacune : `live_preview` sur copie (rien n'est modifié) ; note points, sauts, coupes,
   constats qualité, risque textile et regarde le rendu. Pour le satin, `live_plan_satin` compare
   déjà skeleton / parallel_sweep / medial_axis.
3. Présente un tableau variante × mesures, sans inventer de gain : une mesure absente est dite
   absente. Recommande une variante en expliquant le compromis (qualité visuelle vs coupes, etc.).
4. N'applique que la variante choisie, avec `project_apply` (approbation dans le dock). Le
   point de reprise est le Ctrl+Z ; Ctrl+Y rétablit si l'utilisateur change d'avis.
5. Les checkpoints MCP (`project_checkpoint`) ne sont pas disponibles en mode live : n'en promets pas.
)";

struct WorkspaceSkill {
    const char* dir;
    const char* content;
};

const WorkspaceSkill kExtraSkills[] = {
    {"openstitch-live-diagnose", kSkillDiagnose},   {"openstitch-live-fabric", kSkillFabric},
    {"openstitch-live-order", kSkillOrder},         {"openstitch-live-cleanup", kSkillCleanup},
    {"openstitch-live-naming", kSkillNaming},       {"openstitch-live-review", kSkillReview},
    {"openstitch-live-lettering", kSkillLettering}, {"openstitch-live-layout", kSkillLayout},
    {"openstitch-live-variants", kSkillVariants},
};

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

} // namespace

bool write_workspace_files(const std::string& directory, const std::vector<WorkspaceFile>& files) {
    try {
        const fs::path dir = fs::path(
            std::u8string(reinterpret_cast<const char8_t*>(directory.data()), directory.size()));
        for (const auto& [relative, content] : files) {
            const fs::path rel = fs::path(relative);
            if (relative.empty() || rel.is_absolute() || rel.has_root_name() ||
                rel.has_root_directory() || relative.find("..") != std::string::npos) {
                return false; // jamais d'écriture hors de l'espace de travail
            }
            write_file(dir / rel, content);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

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
        for (const auto& skill : kExtraSkills) {
            write_file(dir / ".claude" / "skills" / skill.dir / "SKILL.md", skill.content);
        }
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
