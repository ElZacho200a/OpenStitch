# 03 — Architecture cible de Desktop V2

Décisions : [ADR 0003 (technologie)](../adr/0003-desktop-v2-technologie.md), [ADR 0004 (architecture)](../adr/0004-desktop-v2-architecture.md).

## Principe directeur

Le moteur offre déjà les bons contrats (voir `01`). Desktop V2 ajoute **quatre modèles partagés** que l'ancien desktop
n'a pas, et fait de chaque widget un *abonné* de ces modèles plutôt qu'un porteur d'état :

```
                    ┌──────────── ui/ (Qt Widgets, aucune logique métier) ────────────┐
                    │ ShellWindow ─ PreviewCanvas ─ arbre documentaire ─ rail d'outils │
                    │      │  s'abonne à ▼              ▲ affiche                       │
                    └──────┼────────────────────────────┼───────────────────────────────┘
   core/ (Qt-free)         │                            │
 ┌────────────────────────▼────────────────────────────┴───────────────────────┐
 │ DocumentSession ── possède ──► Project + UndoStack + SelectionModel           │
 │   execute(ICommand) · applyBatch(JSON) · previewBatch · snapshot() · undo/redo│
 │ TaskManager      tâches nommées, annulables, progression, livraison sur pump()│
 │ ToolRegistry     outils + espaces + raccourcis, une déclaration, conflits testés│
 │ digitize_pipeline / preview_service    services purs exécutés par TaskManager │
 └───────────────┬──────────────────────────────────────────────────────────────┘
                 ▼ (aucune dépendance en sens inverse)
   libs/ : commands · project_ops · document · stitch_generation · formats · stitch_render …
```

## Composants (état réel)

| Composant | Rôle | Fichier | État |
|---|---|---|---|
| **Application Shell** | fenêtre, menus, rail d'outils, barre d'état, câblage des abonnements | `ui/shell_window.*` | ✅ squelette (4 espaces, 2 outils actifs) |
| **Project Session / Document Controller** | seule porte vers le projet vivant ; notifie les abonnés ; élague la sélection | `core/document_session.*` | ✅ |
| **Selection Model** | une sélection ordonnée (région, objet vectoriel, objet de broderie), cible principale = dernière, notifications ré-entrantes sûres | `core/selection_model.*` | ✅ |
| **Command Dispatcher** | deux entrées vers le même historique : `execute(ICommand)` (gestes interactifs) et `applyBatch(JSON)` (lots, MCP, Claude) = **une** étape d'annulation ; `previewBatch` = `dry_run` | `DocumentSession` | ✅ (pas de classe séparée : voir ADR 0004 §3) |
| **Task Manager** | tâches nommées, `stop_token`, progression, résultat livré sur le fil d'interface, jamais si annulée | `core/task_manager.*` | ✅ |
| **Tool System** | registre d'outils × espaces × raccourcis ; gestes de canevas par outil | `core/tool_registry.*` | 🟡 registre ✅, exécution des outils ⬜ |
| **Workspace Management** | espaces = filtres d'attention sur un même document | `ToolRegistry::forWorkspace`, `ShellWindow::setWorkspace` | 🟡 rail ✅, panneaux par espace ⬜ |
| **Canvas Rendering** | aperçu raster d'un instantané ; zoom ancré au curseur ; pan | `ui/preview_canvas.*`, `core/preview_service.*` | 🟡 aperçu ✅, calque d'interaction vectoriel ⬜ |
| **Property Editors** | inspecteur contextuel par type d'objet | — | ⬜ |
| **Notifications / Diagnostics** | ligne d'état persistante (erreur rouge, accessible), progression + annulation | `ShellWindow` | 🟡 minimal ; centre de notifications ⬜ |
| **AI Companion Integration** | session live, aperçu avant/après, approbation | — | ⬜ (la primitive de lot est prête) |
| **Services moteur** | image → DST, aperçu réaliste, export | `core/digitize_pipeline.*`, `core/preview_service.*` | ✅ |

## Règles de dépendance (appliquées par la construction)

1. `core/` n'inclut **aucun** en-tête Qt ; il est construit dans la CI `linux-core` (preuve de non-dépendance).
2. `ui/` dépend de `core/` ; jamais l'inverse. Un widget n'appelle pas `libs/*` pour calculer : il déclenche un service
   de `core/` via `TaskManager` ou une opération via `DocumentSession`.
3. Aucune lib ne dépend de `apps/desktop_v2` ; `apps/desktop_v2` ne dépend pas de `apps/desktop` (le test de parité
   des outils inclut **un seul** en-tête historique, un simple `enum`, pour détecter les dérives).
4. Les bibliothèques tierces restent derrière les libs (Clipper2, OpenCV, DST…) : V2 n'en inclut aucune.

## Flux de référence

**Geste interactif** (renommer dans l'arbre) : widget → `DocumentSession::applyBatch(rename)` → `apply_batch` sur une
**copie** → `ReplaceProjectCommand` (une étape d'annulation) → `changed()` : révision++, sélection élaguée,
abonnés notifiés → arbre mis à jour **en place**, aperçu relancé.

**Calcul long** (numérisation) : `TaskManager::submit` capture la requête **par valeur** → le fil de travail n'accède
jamais à la session → `pump()` (QTimer 33 ms) livre le résultat sur le fil d'interface → `replaceProject`. Annulation :
résultat jamais livré, document inchangé (test `cancelled task never delivers a stale result`).

**Aperçu** : `DocumentSession::snapshot()` (copie immuable partagée par révision) → `render_preview` en tâche →
l'image porte sa révision ; l'interface **écarte** un aperçu dont la révision n'est plus courante.

**Claude** (cible, lot « compagnon ») : le compagnon appelle `previewBatch` (aperçu avant/après, rapport) puis
`applyBatch` après approbation : **exactement** le chemin du renommage ci-dessus. Aucune seconde architecture.

## Contraintes du cahier des charges et leur traitement

| Contrainte | Traitement |
|---|---|
| Mutations via `ICommand`/`UndoStack` | `DocumentSession` est le seul à tenir `Project` modifiable ; `ReplaceProjectCommand` pour les lots |
| `.osp` existants | ouverture/enregistrement via `project_io` (test d'aller-retour) ; aucune évolution de schéma |
| Déterminisme | mêmes fonctions que la CLI ; test « DST identique octet pour octet » sur deux numérisations |
| Unités | `core/` ne manipule aucune coordonnée brute ; mm affichés seulement à la frontière |
| Pas de logique métier dans les widgets | un test de revue : `ui/` n'inclut que `core/` et `project_io` (sauvegarde sur le fil d'interface : **dette connue**, voir journal) |
| Tâches coûteuses hors du fil d'interface | `TaskManager` ; l'enregistrement et le calcul de la liste d'objets restent synchrones (dette connue) |
| Aperçu ≠ modification | `previewBatch` (`dry_run`), `render_preview` sur instantané |
| Pas d'état dupliqué | arbre et canevas lisent `DocumentSession` / `SelectionModel` ; synchronisation par abonnement, test dans les deux sens |
