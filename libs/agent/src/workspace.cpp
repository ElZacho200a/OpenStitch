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

## Mémoire sémantique persistante
`live_context` fournit `semantic_cache` et la clé `semantic` par objet :
- `status=cached` : role/family/confiance déjà mémorisés dans le .osp. RÉUTILISE-les.
- `status=stale` : forme source modifiée ; réexaminer CET objet uniquement.
- champ absent : objet non analysé ; ne regarder que les objets utiles à la demande.
Lorsque tu reconnais un objet nouveau ou périmé, propose un lot `set_semantic`
(`object`, `role`, `family`, `confidence`) pour les mémoriser ensemble.
Une approbation du dock est requise pour ce `project_apply`. La mémoire devient
permanente après enregistrement du projet (.osp) par l'utilisateur.
Ne renomme pas les objets : leur nom utilisateur est indépendant de cette mémoire.

## Analyse sémantique à la demande
Ne lance PAS automatiquement `project_render` ou `live_object_views` à l'ouverture.
Pour une question simple, `live_context` et la sélection suffisent. Si l'utilisateur demande
une analyse sémantique (ou active explicitement l'option d'analyse automatique dans OpenStitch),
utilise `project_render` puis `live_object_views` sur AU PLUS 5 lots de 8 objets
(non reconnus, les plus importants, avec diversité par fil). Ne déduis pas une identité
avec certitude d'une simple couleur : laisse les objets douteux sans annotation.
Propose les annotations via set_semantic avec un degré de confiance et une approbation.

## Arguments exacts des outils MCP (ne jamais les deviner)
Lis le schéma d'entrée de l'outil avant de l'appeler. `object` et `objects`
NE SONT PAS interchangeables. Appels valides (les ID sont des exemples) :
- Vue d'ensemble : `project_render({})` ou
  `project_render({"crop":[0.1,0.2,0.8,0.9],"px_per_mm":4})`.
  `project_render` n'accepte PAS `object`, `objects` ou `object_id`.
- Vues zoomées par objet : `live_object_views({"objects":[12,15]})`
  (1 à 8 identifiants par appel). Jamais `{"object":12}` ici.
- Informations textuelles d'UN objet : `project_inspect({"object":12,"geometry":true})`.
  Qualité d'UN objet : `project_check({"object":12})`.
- Ciblage : `live_target({"query":"orange"})` ;
  filtre de projet : `project_select({"where":["area_lt=3"]})`.
- Comparaison satin : `live_plan_satin({"objects":[12,15]})`.
- Simulation d'une MODIFICATION : `live_preview({"operations":[{"op":"rename",
  "object":12,"name":"Voile"}],"objects":[12]})`. L'ID se trouve DANS l'opération
  et `objects` sert seulement à CADRER le rendu sur les objets modifiés.
  Alternative : `crop:[x0,y0,x1,y1]` et `px_per_mm:4` dans `live_preview`.
- Application (après approbation) : `project_apply({"operations":[{"op":"rename",
  "object":12,"name":"Voile"}]})`. Consulte `project_operations` pour les
  arguments réels de chaque opération (dont `set_semantic`).
Pour une reconnaissance sémantique SANS modification de points, `set_semantic`
est une métadonnée : inutile d'appeler `live_preview` ou de rendre l'image
pour chaque annotation. Propose le lot, puis `project_apply` avec approbation.
IMPORTANT : `live_preview` est une SIMULATION ; ne dis JAMAIS « c'est fait »,
« les objets sont passés à » ou « j'ai changé le projet » avant un retour
`project_apply` réussi. Ne demande jamais d'appliquer simplement pour obtenir
un zoom : `live_preview({"operations":[...],"objects":[12,15]})` zoome sur
la COPIE modifiée, avant l'approbation.
Pour « remplissage spirale » utilise le type NATIF `spiral_fill`, PAS un
`directional` aux guides en spirale. Dans un MÊME lot : convertis chaque objet
par `set_stitch_type` (type `spiral_fill`) puis `set_params` avec
`{"center_mm":[x,y]}`. Le centre est une position [X,Y] en mm dans le
repère DU PROJET (pas dans la boîte propre à chaque forme) : réutilise exactement
le même centre pour les objets du même sujet, sans l'inventer si inconnu.
Le champ `center_mm` ne s'applique pas aux `directional`.
Si un outil refuse un argument, corrige en relisant SON schéma / catalogue ;
ne multiplie pas les essais de clés supposées ni les rendus coûteux.

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
3. `live_plan_satin({"objects":[12,15]})` : compare skeleton / parallel_sweep /
   medial_axis, justifie les refus et propose un lot de corrections.
4. `live_preview({"operations":[...],"objects":[12,15]})` : avant/après
   zoomé des MODIFICATIONS de couture, sur une copie. Aucun effet sur le projet.
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

1. `live_context({})` puis `live_target({"query":"orange"})` pour cibler les objets.
2. `project_operations({})` pour le catalogue ; prépare un lot `operations`.
3. `live_preview({"operations":[...],"objects":[12]})` avec ce lot : le
   tableau `objects` cadre l'image, il n'indique PAS les objets à modifier.
   Montre l'avant/après à l'utilisateur avant de proposer l'application.
4. `project_apply` (sans dry_run) : l'approbation se fait dans le dock ; annulable en un Ctrl+Z.
   Tant que l'outil ne confirme pas `applied=true`, le projet reste inchangé.
5. `project_check` et `project_render` pour vérifier. Si l'utilisateur rejette, ne réessaie pas
   le même lot : demande ce qui ne convient pas.
)";

constexpr const char* kSkillSemantic = R"(---
name: openstitch-live-semantic
description: Analyse sémantique du projet OpenStitch ouvert - identifier à quoi correspond chaque objet de broderie (pétale, feuille, contour…) à partir de vues zoomées, puis proposer de les nommer. À utiliser uniquement sur demande ou si l’option d’analyse automatique est activée.
---

# Analyse sémantique du motif

1. `live_context` : objets, fils, familles de couleur, sélection.
2. `project_render({})` UNE FOIS si une vue d'ensemble est nécessaire.
   Il n'accepte pas `object` ; le zoom d'un objet se fait avec `live_object_views`.
3. `live_object_views({"objects":[12,15]})` pour les objets sans cache valide,
   au plus 5 lots de 8 IDs. Pour le descriptif textuel d'un seul objet, utilise
   `project_inspect({"object":12,"geometry":true})`.
4. Table finale : identifiant, rôle reconnu, confiance, fil. Les objets douteux
   restent non annotés ; ne devine pas leur nature à partir de la couleur seule.
5. Propose un lot `project_apply({"operations":[{"op":"set_semantic",
   "object":12,"role":"voile","family":"bateau","confidence":0.9}]})`
   pour les objets réellement reconnus ; l'utilisateur doit approuver.
   Ne lance pas `live_preview` pour ces seules métadonnées : aucun point ne change.
)";

constexpr const char* kSkillSatin = R"(---
name: openstitch-live-satin
description: Convertir des formes en satin avec compensation de tirage Predicted sur le projet OpenStitch ouvert (ciblage par fil, comparaison des 3 stratégies, refus justifiés). À utiliser pour « mets ces formes en satin ».
---

# Passer des formes en satin

1. `live_target({"query":"orange"})` ; si `ambiguous`, `live_ask_user`.
2. `live_plan_satin({"objects":[12,15]})` (20 IDs max) : lis `targets[].verdict` ;
   `refused` reste en tatami, `skipped` = verrouillé, masqué ou déjà satin.
3. Passe le lot retourné dans `live_preview({"operations":[...]})`
   (la valeur de `batch.operations` est placée sous la clé `operations`)
   et compare points, sauts, constats et risque textile.
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
2. `project_diagnose` : constats détaillés avec objet et suggestion ;
   `project_check({"object":12})` pour UN objet (`object` singulier) ;
   `project_density` (couches, surépaisseurs) ; `project_fidelity` (trous intérieurs,
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

1. Repère : `project_select({"where":["area_lt=3"]})` pour les objets de
   moins de 3 mm² ; `project_locate({"x_mm":0,"y_mm":0})` pour identifier les
   coutures autour d'un point ; `project_diagnose` pour les suggestions.
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
4. Pour renommer, `live_preview({"operations":[...]})` puis
   `project_apply({"operations":[...]})` après accord. Si seule la mémoire
   `set_semantic` change, l'aperçu est inutile ; l'approbation reste requise.
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
