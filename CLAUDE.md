# CLAUDE.md

OpenStitch Studio : application Windows libre de numérisation de broderie machine
(image → régions vectorielles → objets → points → DST). C++23, Qt 6 Widgets pour
l'IHM seulement, Apache-2.0. Documentation : `docs/source/*.md` (un chapitre par
sujet) ; garde-fous : `GUARDRAILS.md`.

## Coordination Notion — obligatoire pour tout travail autonome

Le [Project HQ Notion](https://app.notion.com/p/411bb207e2da818686c3f8d6db4991d5) et ses bases existantes pilotent les priorités, réservations, dépendances et validations. GitHub reste la source de vérité du code, des PR et de la CI. Lire `AGENTS.md`, `docs/agents/notion-protocol.md` et le skill `openstitch-notion` pour « continue », « prochaine tâche », audit d'avancement ou délégation. **Au début de chaque nouvelle session, récupérer les skills Notion activés via `notion-get-enabled-skills` (ou outil équivalent) quand disponible.**

Avant tout nouveau lot autonome : inspecter les tâches existantes, propriétaires, baux, PR et CI, puis réserver un périmètre disjoint avec relire-après-écriture. Les baux Notion sont souples et non atomiques ; toute concurrence exige un arbitrage par un orchestrateur unique. Pour une demande ciblée, la consigne de l'utilisateur reste prioritaire ; annoncer les limitations si Notion manque.

**Validation :** ne jamais déduire « Validé » du seul merge : critères, commit main, tests/CI, revue indépendante et recette Studio/physique selon le cas. Une PR en cours reste « En PR ». Ne pas annoncer une réussite sans preuve. Les règles locales de non-merge de ce fichier restent en vigueur.

## Local first — Claude supervise, Ollama exécute (règle permanente)

Économiser le quota Anthropic : le travail technique (exploration, lecture, bugs, refactoring, implémentation, tests, build/CI, relecture, doc, messages de commit/PR) est délégué à Ollama/OpenCode via `scripts/local/delegate.ps1` (niveaux 0-5, rapport compact, statut calculé mécaniquement). Claude comprend, découpe, vérifie, arbitre ; GitHub et Notion restent à Claude. Un sous-agent Claude n'est pas local. Politique et escalade : `.claude/rules/local-first.md` ; skill `openstitch-local-first` ; stats : `delegate.ps1 -Stats`. Un hook signale (sans bloquer) les éditions directes de code et les sous-agents.

## Invariants (violation = reset)

- **Couches** strictes et acycliques : `apps/{desktop,cli} → libs/* → libs/core`. Seul
  `apps/desktop` dépend de Qt ; le cœur et le CLI se compilent sans Qt (CI `linux-core`).
  Aucun code GPL/AGPL (Ink/Stitch, Potrace, Poppler, Ghostscript, MuPDF exclus).
  Les libs tierces restent derrière une interface interne (Clipper2 dans `geometry`,
  OpenCV dans `image`/`segmentation`/`vectorization`, DST dans `formats`, JSON/minizip
  dans `project_io`).
- **Aucune coordonnée en `double` brut** : `Micrometers` (`int32`) / `Vec2um`, Y vers le
  haut. Conversions aux frontières seulement (px→µm à l'import, µm→mm à l'affichage,
  µm→0,1 mm pour le DST).
- **Toute mutation du document passe par une `commands::ICommand`** poussée sur l'`UndoStack`.
- **Les points ne sont jamais stockés** : tout consommateur appelle
  `stitch_generation::effective_sequence(project)`, jamais `generate_sequence` (garde
  CTest `check_no_raw_sequence_bypass`, exception annotée `raw-sequence-ok:`).
- **`apps/desktop` sans logique métier** : câblage, rendu, déclenchement. Calculs longs
  hors du fil d'interface via `BusyIndicator::run`.
- **Déterminisme** : même entrée → sortie identique octet pour octet. Pas de conteneur
  non ordonné dont l'ordre fuit ; parallélisme via `openstitch::parallel_for` (tâches qui
  lisent des données constantes et écrivent leur propre case ; assemblage séquentiel).
- Jamais de commit sur `main` ; jamais de merge de PR ; PR contre `main`.

## Build & tests

Prérequis : VS 2022+, CMake ≥ 3.27, vcpkg, Qt 6.8.3 ; variables `VCPKG_ROOT`, `QT_ROOT`.
Presets : `msvc` (configure), `msvc-debug`, `msvc-release`. **Itération : cible + exécutable
de test seulement ; suite complète avant livraison.** Commandes, table lib → exécutable,
sorties compactes (`scripts/qrun.ps1`) : skill `openstitch-build-test`.

Format : clang-format **18** exactement (la 23 locale fait échouer la CI) ; `-i` sur les
fichiers modifiés seulement.

## Recherche et documentation (économiser le contexte)

- Chercher avant de lire : Grep/Glob ou `mesh-mcp` (`search_docs` pour `docs/`,
  `analyze_impact("<symbole>")` pour les appelants et CMake ; ses parseurs ne couvrent pas le
  C++, donc Grep reste l'outil des `.cpp`/`.hpp`). `rg` (ripgrep, installé via winget) est disponible après
  redémarrage du shell ; l'outil Grep reste le défaut. `rtk proxy <cmd>` : sortie brute si un filtre cache l'erreur.
- Lire par plages (`offset`/`limit`). `satin.md` (~350 Ko), `satin-squelette.md`,
  `moteur-de-points.md` : jamais en entier ; skill `openstitch-docs` pour la méthode et le repli.
- Satin / auto-satin : lire la ou les sections concernées (jonctions, routage, guides,
  squelette), pas les chapitres entiers ; l'audit global seul justifie une lecture complète.

## Skills du projet (`.claude/skills/`)

Chargés à la demande selon leur description : `openstitch-docs`, `-build-test`,
`-stitch-param`, `-cli-debug`, `-bugfix`, `-commit`, `-performance`, `-digitize`,
`-multi-agent`, `-roadmap`, `-token-efficiency`, `-notion`, `-studio-ux`. Règles qui s'appliquent même sans les charger :

- **Chaque commit met à jour `docs/roadmap-parite-hatch.md`** (statut, nouveaux manques,
  journal §28) ou écrit « roadmap : aucune entrée concernée » dans le message.
- **Toute fonctionnalité est câblée dans `apps/desktop`** (menu/outil/dialogue/panneau,
  `ICommand`, test QTest headless), sauf demande contraire explicite.
- **Trois modes de session** (détail : `openstitch-commit`, `openstitch-roadmap`) :
  *mission ciblée* (défaut : faire la demande, valider, commit, push, PR, s'arrêter),
  *autonomie continue* (enchaîner les lots de la roadmap, uniquement si demandé : « continue »,
  `/loop`), *audit* (examiner et corriger la configuration, sans partir sur une feature).
- Sous-agents : la délégation doit être rentable (`openstitch-multi-agent` : agents
  `os-explorer` haiku, `os-dev` sonnet, `os-architect` opus).

## Tests

Catch2 v3 (cœur/CLI, `tests/unit/<lib>/test_*.cpp`), QTest headless pour l'IHM
(`QT_QPA_PLATFORM=offscreen`, ni pixels ni `sleep`). **Noms de `TEST_CASE` en ASCII pur.**
SVG dorés de `tests/golden/` : jamais réécrits par un test (voir `openstitch-cli-debug`).

## Pièges connus

- Header spdlog : `stdout_color_sinks.h`. Pas de membre nommé `slots` (macro Qt). Classes Qt
  déclarées en avant au **scope global**. Pas de `M_PI` sous MSVC : `std::numbers::pi`.
- Cibles vcpkg exactes : `Clipper2::Clipper2`, `MINIZIP::minizip-ng`.
- `RasterTransform` et conversions µm↔pixel de `libs/auto_satin` tronquent vers zéro.

## Reprise de session

Contexte trop long ou changement de session : écrire un point de reprise de ≤ 15 lignes
(objectif, décisions, branche/commit, tests passés, prochaine action) — voir
`openstitch-token-efficiency` §4 — plutôt que de recopier l'historique.

## Carte des modules

`core`, `geometry`, `image`, `segmentation`, `vectorization`, `document`, `stitch`,
`stitch_generation`, `stitch_render`, `auto_satin`, `stitch_analysis`, `optimization`,
`autodigitize`, `commands`, `formats`, `project_io`, `project_ops` (API de
`openstitch-cli project …`). Responsabilités et « où changer X » :
`docs/source/module-reference.md` (via `search_docs`).
