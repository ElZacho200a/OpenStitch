# Serveur MCP (`openstitch-mcp`) : OpenStitch pour les agents IA

Public : agent IA, intégrateur. `openstitch-mcp` expose `libs/project_ops` comme **outils
typés** du protocole MCP (Model Context Protocol, JSON-RPC 2.0 sur stdio). Il complète la CLI
`openstitch-cli project …` (voir `cli-project.md`) sans la remplacer : la CLI reste la référence
scriptable, le serveur évite à l'agent de relancer un processus et de recharger le `.osp` à chaque
étape, et lui fournit des schémas d'arguments, des erreurs actionnables et un **rendu image**.

## Architecture

```
apps/mcp (main.cpp)       transport stdio : une ligne JSON-RPC par message
        │
libs/mcp                  protocole, session, outils (Qt-free, JSON encapsulé)
        ▼
libs/project_ops          fabrique, inspection, lots transactionnels, qualité, rendu
```

- **Aucun algorithme réimplémenté** : chaque outil appelle `project_ops` ; toute mutation passe
  par `apply_batch` donc par des `commands::ICommand`.
- **Une session = un projet ouvert en mémoire** (`project_open`, `project_new` ou
  `project_digitize`). Un lot refusé laisse le projet strictement inchangé.
- **stdout ne porte que le protocole.** Le descripteur 1 est redirigé vers stderr au démarrage
  (OpenCV journalise sur stdout) ; les réponses partent sur une copie privée.
- Sans Qt (`linux-core`). Déterminisme : mêmes appels → mêmes réponses (aucun horodatage).

## Installation Linux x86-64 (release vérifiée)

Depuis un clone du dépôt :

```bash
bash scripts/install-mcp-linux.sh
```

Le script télécharge la release `latest` ou un artefact CI réussi de `main`,
vérifie SHA-256, les dépendances et l'initialisation MCP, puis installe
`~/.local/bin/openstitch-mcp`. Le serveur utilise **stdio**, sans ouverture
de port réseau. Les archives `openstitch-mcp-linux-x86_64.tar.gz` et
`.sha256` sont publiées dans la release après réussite des tests CI.

## Installation côté client

```json
{ "mcpServers": { "openstitch": { "command": "C:/chemin/openstitch-mcp.exe" } } }
```

(fichier `.mcp.json` d'un projet Claude Code, ou configuration équivalente d'un autre client).

## Outils

| Outil | Rôle |
|---|---|
| `project_operations` | catalogue des opérations (noms, arguments, unités, plages, exemple) |
| `project_new` | projet vide (cadre, finitions) ; écrit si `path` |
| `project_open` | ouvre un `.osp` |
| `project_digitize` | image → projet ; `settings` = options de `project digitize` (tirets → `_`) ; réglage inconnu → refus qui liste les réglages valides ; **cadre ajusté au motif** (`fit_canvas`, `margin_mm`) ; `notes` si un réglage n'a pas pu agir |
| `project_inspect` | **résumé compact** par défaut (cadre, points, objets groupés par type puis couleur, ~1,5 Ko) ; `detail=full` (~100 Ko) ; `object` (+ `geometry`, `stitches`) pour un objet |
| `project_select` | identifiants filtrés : `color=`, `type=`, `area_gt=` / `area_lt=` (mm²), `within=`, `name=`, `visible`, `locked` |
| `project_apply` | lot d'opérations transactionnel ; `dry_run` ; alias `as` / `$alias` ; `skip_refused` : écarte les opérations refusées (listées dans `skipped`) et applique le reste |
| `project_palette` | palette réellement cousue, dans l'ordre de couture (blocs), objets / aire / part de segmentation par couleur |
| `digitize_guide` | guide de numérisation (voir ci-dessous) : à appeler avant de numériser |
| `semantic_validate` / `semantic_import` / `semantic_replace` / `semantic_map` | manifeste `openstitch.semantic/1` + masques PNG -> objets distincts qui gardent leur identité (voir *Sémantique native*) |
| `project_report` | bilan en un appel : statut explicite par analyse, dimensions, points, coupes, défauts par objet |
| `project_diagnose` / `project_density` / `project_fidelity` / `project_fabric_risk` / `project_locate` | analyses du moteur (mêmes fonctions que la CLI) |
| `project_checkpoint` / `project_restore` / `project_checkpoints` / `project_compare` | variantes en mémoire, retour arrière sûr, comparaison à même échelle |
| `project_satin_compare` | skeleton / parallel_sweep / medial_axis sur la même géométrie, copies isolées, aucun choix automatique |
| `project_fit_canvas` | ajuste le cadre au motif (origine au centre) : supprime les erreurs « hors cadre » |
| `project_check` | contrôle qualité (`openstitch.quality/1`), `object` pour un seul objet |
| `project_render` | **image PNG** du rendu réaliste ; `crop` [x0,y0,x1,y1] pour zoomer, `px_per_mm` (défaut 3, plafonné à 4 Mpx) |
| `project_save` | écriture atomique du `.osp` (sans `path` : chemin d'ouverture) |
| `project_export` | fichier machine (`dst` par défaut) |

Boucle conseillée : `project_digitize` → `project_render` (voir) → `project_check` (mesurer) →
`project_apply` (`dry_run` d'abord si doute) → `project_check` / `project_render` → `project_save`
→ `project_export`.

### Recette : satin sur des formes claires allongées

Mesurée sur `tests/fixtures/tentabrode.png` (`dpi` 284, soit ~100 mm de large) : après
`project_digitize`, `project_select` `["color=#E8DBB0","type=tatami","area_gt=6"]` (anneaux) puis
`project_apply` d'`auto_satin` (`medial_axis`) avec `skip_refused` ; idem `parallel_sweep` sur les
brins d'algue. 3 anneaux sur 13 sont refusés (formes compactes) et restent en tatami ; 11 915
points, 0 erreur qualité. Les objets de moins de ~6 mm² restent en tatami (rendu polygonal) : les
exclure avec `area_gt`. Sur ces anneaux le **trou central disparaissait** (cousus en disques) :
ce n'est pas un défaut du moteur mais le défaut `min_region_mm2=3`, qui fusionne toute région
plus petite que 3 mm² avec sa voisine. `min_region_mm2=0.3` garde les trous (9 → 19 trous clairs,
163 objets au lieu de 106, 11 953 points) ; ajouter `curve_tolerance_mm=0.05` lisse les contours. `rings_satin` ne s'applique pas à ce cas (anneaux **sombres** seulement,
avec `dark_lines`).

## Guide de numérisation embarqué (skill `openstitch-digitize`)

Le skill `.claude/skills/openstitch-digitize/` (méthode sémantique en 10 phases + références) est
**la** source de vérité : le build l'intègre à `openstitch-mcp` (génération à la configuration,
aucune copie à maintenir ; un test compare l'embarqué aux fichiers). Un client MCP qui ne lit pas
les skills de Claude Code y accède par quatre voies :

| Voie | Automatique ? |
|---|---|
| `initialize.instructions` : principes en quelques lignes + « appelle `digitize_guide` avant de numériser » | oui, injecté par la plupart des clients |
| outil `digitize_guide` (sans argument : sommaire ; `topic=workflow`, `mcp`, `selection-satins`, `parametres`…) | l'agent l'appelle sur la consigne des instructions |
| ressources `openstitch://guide/<sujet>` | selon le client |
| prompt `digitize` (`image`, `width_mm`) | lancé à la main (commande du client) |

Le sujet `mcp` traduit les commandes du guide en outils (`project_palette` remplace
`osp_palette.py`) et liste ce qui manque encore côté MCP (`sweep`, `diagnose`, `fidelity`,
`density`, `at`, `diff`, `fabric-risk`).

## Sémantique native

Le manifeste (`docs/agent-semantic-format.md`) décrit des régions : `semantic_id` stable, famille,
parents, `under` / `over` (occlusion), `thread_rgb`, `stitch_intent` (`tatami`, `running`,
`auto_satin`) et un masque PNG (blanc > 127 sur noir, mêmes dimensions que l'image source).

- `semantic_validate` : mêmes codes d'erreur que `scripts/openstitch_semantic.py` (`E_SCHEMA`,
  `E_MASK_SIZE`, `E_OCCLUSION_CYCLE`…) ; ne modifie rien.
- `semantic_import` : un objet par contour, **même à couleur de fil identique** (l'identité n'est
  jamais la couleur), nommé `sem:<id> <n>`, créé dans l'ordre d'occlusion. Tout est vérifié avant
  la première mutation ; un refus (masque absent, proportions incohérentes, `auto_satin`
  impossible) laisse le projet strictement inchangé. `dry_run` simule.
- `semantic_map` : provenance identifiant sémantique -> objets et formes sources. Elle est portée par
  le **nom** de l'objet : sauvegardée dans le `.osp` sans changement de format, compatible avec les
  projets du pont Python.
- `semantic_replace` : remplace un seul identifiant à partir d'un masque corrigé, **à la même place
  dans l'ordre de couture** ; les autres objets restent identiques (testé). Les identifiants
  numériques de l'objet remplacé changent, son identité sémantique reste.

Ces opérations sont des opérations du registre : elles marchent aussi par `openstitch-cli project
op|apply` (parité CLI/MCP, mêmes refus). Limites : `directional` et `satin` exigent des guides (poser
ensuite par `project_apply`) ; les métadonnées (famille, parents, priorités) restent dans le
manifeste, pas dans le `.osp`.

## Diagnostics

`project_report` classe chaque analyse en `pass`, `warning`, `error`, `not_run` (exclue par `only`)
ou `unsupported` (prérequis absent : p. ex. `fidelity` sans image source embarquée, jamais de valeur
inventée). Les seuils de `fidelity` (2 % de trous intérieurs ou de détail perdu) et les points chauds
de densité / de risque textile sont **indicatifs** ; `fabric_risk` est une heuristique non calibrée.
Les défauts de `project_diagnose` sont regroupés par objet (`findings_by_object`).

## Variantes et comparaison

`project_checkpoint` garde l'état courant sous un nom (12 au plus ; `path` l'écrit aussi en `.osp`,
jamais par écrasement). `project_restore` garde l'état courant sous `_avant_restauration` avant de
revenir en arrière. `project_compare` donne les statistiques côte à côte, le diff du moteur par objet
et deux rendus à même échelle et même zone. `project_satin_compare` ne modifie jamais la session.

## Installation portable et diagnostic

```bash
openstitch-mcp --version      # version du serveur
openstitch-mcp --doctor       # auto-test en mémoire : handshake, outils, projet, rendu, guide (code 0 / 2)
```

Configuration de client **sans chemin propre à un développeur** (le binaire est dans le `PATH`) :

```json
{ "mcpServers": { "openstitch": { "command": "openstitch-mcp" } } }
```

Sous Windows, remplacer par le chemin du `.exe` (ou l'ajouter au `PATH`) ; sous WSL, lancer le binaire
Linux ou `cmd.exe /c openstitch-mcp.exe`. Compilation : cible `openstitch-mcp` (voir `installation.md`
et `build-system.md`). Transport stdio uniquement : aucun port réseau n'est ouvert.

## Erreurs

Un refus est un résultat d'outil `isError: true` dont le texte est le document
`openstitch.error/1` (`code`, `message`, `hint`). Codes de la CLI (`E_*`, voir `cli-project.md`) plus
`E_NO_PROJECT` (aucun projet ouvert dans la session) et `E_UNKNOWN_TOOL`. Les erreurs de protocole
(JSON invalide, méthode inconnue) suivent les codes JSON-RPC standard.

## Robustesse (audité)

- Entrées invalides : JSON illisible, lot JSON-RPC (tableau), `arguments` non objet, identifiant
  d'objet non entier ou ≤ 0, `crop` hors de [0,1], réglage inconnu → refus nommé, jamais de plantage ;
  une exception inattendue renvoie l'erreur JSON-RPC -32603 et le serveur continue.
- Chemins : UTF-8 sur toutes les plateformes (dossiers accentués inclus).
- Rendu borné à 1,5 Mpx (PNG ~1,5 Mo) ; `skip_refused` borné à 500 opérations par appel.
- Un échec n'altère jamais le projet de la session (numérisation, ouverture, lot refusé).

## Limites connues

- Un seul projet par session ; pas d'annulation pas à pas (relire le `.osp` avec `project_open`
  pour revenir à l'état sauvegardé ; `dirty` signale les modifications non enregistrées).
- Pas de lots JSON-RPC, pas de notifications de progression : les numérisations longues bloquent
  l'appel jusqu'au résultat.
- Les très petits objets (< ~6 mm²) sont mal rendus en satin comme en tatami : limite du moteur,
  non traitée ici.
- Outils d'analyse fine (`fabric-risk`, balayage d'options, localisation) pas encore exposés :
  à ajouter au besoin dans `libs/mcp/src/tools.cpp`.
