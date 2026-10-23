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
| `project_digitize` | image → projet ; `settings` = options de `project digitize` (tirets → `_`) ; réglage inconnu → refus qui liste les réglages valides |
| `project_inspect` | structure (objets, types de points, couleurs) ; `object`, `geometry`, `stitches` |
| `project_apply` | lot d'opérations transactionnel ; `dry_run` ; alias `as` / `$alias` |
| `project_check` | contrôle qualité (`openstitch.quality/1`), `object` pour un seul objet |
| `project_render` | **image PNG** du rendu réaliste ; `crop` [x0,y0,x1,y1] pour zoomer, `px_per_mm` (défaut 3, plafonné à 4 Mpx) |
| `project_save` | écriture atomique du `.osp` (sans `path` : chemin d'ouverture) |
| `project_export` | fichier machine (`dst` par défaut) |

Boucle conseillée : `project_digitize` → `project_render` (voir) → `project_check` (mesurer) →
`project_apply` (`dry_run` d'abord si doute) → `project_check` / `project_render` → `project_save`
→ `project_export`.

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
- Outils d'analyse fine (`fabric-risk`, balayage d'options, localisation) pas encore exposés :
  à ajouter au besoin dans `libs/mcp/src/tools.cpp`.
