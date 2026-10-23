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

## Erreurs

Un refus est un résultat d'outil `isError: true` dont le texte est le document
`openstitch.error/1` (`code`, `message`, `hint`). Codes de la CLI (`E_*`, voir `cli-project.md`) plus
`E_NO_PROJECT` (aucun projet ouvert dans la session) et `E_UNKNOWN_TOOL`. Les erreurs de protocole
(JSON invalide, méthode inconnue) suivent les codes JSON-RPC standard.

## Limites connues

- Un seul projet par session ; pas d'annulation pas à pas (relire le `.osp` avec `project_open`
  pour revenir à l'état sauvegardé ; `dirty` signale les modifications non enregistrées).
- Pas de lots JSON-RPC, pas de notifications de progression : les numérisations longues bloquent
  l'appel jusqu'au résultat.
- Les très petits objets (< ~6 mm²) sont mal rendus en satin comme en tatami : limite du moteur,
  non traitée ici.
- Outils d'analyse fine (`fabric-risk`, balayage d'options, localisation) pas encore exposés :
  à ajouter au besoin dans `libs/mcp/src/tools.cpp`.
