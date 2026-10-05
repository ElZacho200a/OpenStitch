# Project Guardrails — OpenStitch Studio

Ces garde-fous encadrent les agents Liza (doers, reviewers, orchestrateur)
qui travaillent sur ce dépôt. Ils sont dérivés de `CLAUDE.md` et de l'en-tête
de `docs/roadmap-parite-hatch.md` — en cas de doute, ces deux fichiers font
foi.

## Tier 0 (Inviolable — déclenche un RESET si violé)

- Ne jamais ajouter de dépendance GPL/AGPL, ni liée ni copiée : le projet est
  Apache-2.0 strict.
- Le cœur (`libs/`, `apps/cli`) ne dépend jamais de Qt. Toute modification du
  cœur doit rester buildable par le preset CMake `linux-core` (garde-fou de
  portabilité vérifié en CI, sans Qt).
- Ne jamais committer directement sur `main` : tout le travail des agents
  passe par des worktrees isolés et une branche d'intégration.
- Ne jamais introduire de mutation du document (`Project`, objets de
  broderie) hors d'une commande `ICommand` — l'undo/redo doit rester correct.

## Tier 1 (Contraintes fortes — suspension seulement avec dérogation explicite)

- Les points de broderie ne sont jamais stockés en dur : tout consommateur de
  la séquence de points passe par `effective_sequence`, jamais directement
  par `generate_sequence`. `analyze_impact("effective_sequence")` via
  mesh-mcp liste les appelants existants.
- Coordonnées en micromètres entiers uniquement (pas de float pour les
  positions du document).
- Déterminisme : à entrée égale (image, paramètres), la génération de points
  doit produire une sortie identique — pas de dépendance à l'ordre
  d'itération non spécifié, aux timestamps, etc.
- `git ls-files '*.cpp' '*.hpp' | xargs clang-format --dry-run --Werror` doit
  passer avant tout commit (mêmes règles que le job CI `format`).
- Toute modification du sous-système satin (skeleton extraction, ancrage des
  jonctions, routage, guides) nécessite d'avoir lu
  `docs/source/satin.md` au préalable — c'est la référence technique
  détaillée, avec les root-cause writeups des bugs déjà corrigés.

## Tier 2 (Défauts forts — best-effort sous contrainte de temps)

- Ne pas faire régresser la couverture de tests existante (`ctest --preset
  msvc-debug` / `linux-core` doivent rester verts).
- Respecter le format d'entrée de `docs/roadmap-parite-hatch.md` (§0.1) pour
  toute nouvelle entrée ou mise à jour de statut, y compris les critères
  d'acceptation vérifiables.
- Documenter dans `docs/source/*.md` toute décision non triviale sur un
  sous-système déjà documenté (satin, auto-satin, tatami, remplissage
  directionnel, overrides, format `.osp`, DST…).
- Pas de régression de performance mesurable sur les benchmarks existants
  (`docs/performance-audit.md`, `docs/stitch-engine-audit.md`) sans
  justification explicite.

### G2.x: Lessons — Agents

Operational lessons from project experience. Read when a trigger matches.

| Trigger | File |
|---------|------|
| When a liza lifecycle command fails with 'agent generation required' in a Windows-hosted agent session | [liza-generation-lost-wsl-to-windows.md](lessons/agents/liza-generation-lost-wsl-to-windows.md) |

## Tier 3 (Préférences — dégradées gracieusement sous pression)

- Suivre les conventions de commit existantes : message conventionnel en
  français (`feat(scope): ...`, `fix(scope): ...`, `perf(scope): ...`,
  `refactor(scope): ...`, `docs(scope): ...`, `chore: ...`).
- Préférer un correctif minimal et ciblé à une réécriture large, en
  particulier sur le moteur de points (satin/tatami/auto-satin).
- Utiliser `openstitch-cli` (stitchdebug, auto-satin-debug, dst2svg) pour
  valider visuellement un changement de génération plutôt que de se fier au
  seul rendu de l'IHM.
