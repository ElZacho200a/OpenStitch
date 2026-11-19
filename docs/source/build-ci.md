# Intégration continue (CI)

Public : développeur, mainteneur. Chantier Notion BUILD-OPT-01.7. Workflow : `.github/workflows/ci.yml`.

## Jobs

| Job | Rôle | Exécuté |
|---|---|---|
| `changes` | Aiguillage : classe les fichiers de la PR (API GitHub, quelques secondes) | toujours |
| `windows-msvc` | Build Release, QA fondus, Studio, `ctest`, packaging MCP, artefacts | si `code=true` |
| `linux-core` | Cœur + CLI sans Qt, tests, installateur, paquets Linux, artefacts | si `code=true` |
| `format` | clang-format 18 (vérification seule) | toujours |
| `ci-gate` (« CI complète ») | Point de contrôle unique, à exiger dans la protection de `main` | toujours |
| `release` | Publication « latest » | push sur `main` seulement, après `ci-gate` |

## Politique PR rapide / validation complète

- **PR sans code** : si tous les fichiers modifiés sont dans `.claude/`, `docs/agents/`, `docs/ai/`,
  `docs/assets/`, `.github/ISSUE_TEMPLATE/` ou sont des `*.md` à la racine, les deux builds lourds sont
  sautés (`code=false`). Seuls `format` et `ci-gate` tournent.
- **Tout le reste est du code** : `docs/source/`, `docs/desktop-v2/`, `docs/adr/`, workflows, CMake,
  vcpkg, scripts… (certains tests CTest lisent ces fichiers). En cas de doute ou d'échec de l'API,
  `code=true`.
- **Push sur `main`** : toujours complet ; la release en dépend.
- **Aucun test n'est retiré** : `windows-msvc` et `linux-core` gardent exactement leurs étapes.
- `ci-gate` échoue si `changes` ou `format` échouent, si un build lourd échoue ou est annulé alors qu'il
  devait tourner, ou s'il est sauté alors qu'il devait tourner. Un job sauté n'est donc jamais pris
  pour un succès, sauf pour une PR classée sans code.

## Autres garde-fous

- Annulation des runs obsolètes d'une PR (`concurrency`) ; les pushes `main` ne sont jamais annulés.
- `permissions: contents: read` par défaut ; `release` seul a `contents: write`.
- `timeout-minutes` sur chaque job (120 Windows, 90 Linux) : un job bloqué ne monopolise plus un runner 6 h.
- Les nouvelles étapes de cache vcpkg (BUILD-OPT-01.5) et sccache (01.4) se placent dans les jobs existants.

## À configurer côté GitHub

Remplacer, dans la protection de branche de `main`, les contrôles requis `windows-msvc` / `linux-core` /
`format` par **« CI complète »** : sans cela une PR sans code resterait bloquée sur des contrôles sautés
(ou, à l'inverse, un contrôle sauté pourrait être accepté).

## Vérifications au premier run réel (non exécutable sans pousser)

1. `changes` : sortie `code` correcte sur une PR de code (true) et sur une PR de seul `.md` racine (false).
2. `gh api .../pulls/N/files` autorisé avec `pull-requests: read` (sinon avertissement, repli `code=true`).
3. `ci-gate` vert dans les deux cas ; rouge si l'on casse volontairement un test.
4. `release` s'exécute encore sur `main` (dépend de `ci-gate`, qui tourne avec `always()`).
5. Les timeouts ne coupent pas un run froid (cache vcpkg vide) ; relever la durée réelle.

Validation locale : analyse YAML uniquement (`actionlint` indisponible sur la machine de rédaction).

## Suivi du plantage MSVC C1001

`generate_boundary_blend_layer` (`libs/stitch_generation/src/generate.cpp`) plantait MSVC 14.50 en Release
sur un lambda à `if constexpr` imbriqué dans un visiteur générique ; corrigé par #408 (surcharges
`boundary_underlay_for`). Recherche d'autres occurrences du motif dans `libs/` : seul
`predicted_pull.cpp:39` contient un lambda générique à `if constexpr (requires …)` retournant une valeur,
mais il est à un seul niveau, hors visiteur imbriqué, et compile en CI ; à surveiller sans modification.
Si un C1001 réapparaît : remplacer le lambda par des surcharges ou une fonction nommée.
