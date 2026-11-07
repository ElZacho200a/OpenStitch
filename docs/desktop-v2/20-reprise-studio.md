# Point de reprise — migration Studio

- Branche de travail : `claude/studio-lot1-parcours-edition` (PR #283, base `codex/gpu-native-studio` = PR #282).
  Contient : #282 (shell Qt Quick/GPU), lot 1 (nouveau/ouvrir/enregistrer/undo/déplacer clavier),
  `main` à jour (#280 fusionnée), #279 (inventaire V1 automatisé, services filtres/simulation/nuancier).
- Ordre d'intégration retenu : #283 (porte #282 + #279 + #280) → `main`. #282 et #279 seules sont en
  conflit avec `main` ; #283 les résout. Autres PR ouvertes : #236 (découpage main_window V1, conflit,
  à traiter à la bascule), #152 et #131 (validation/juge visuel, indépendantes).
- Matrice : `python scripts/parity/build_studio_matrix.py` → 444 fonctions V1 : 0 VALIDATED,
  6 TESTED, 1 FUNCTIONAL_UNTESTED, 276 SERVICE_ONLY, 150 ABSENT, 11 BLOCKED (exclusions à arbitrer).
  Reste à ajouter : capacités V2-only, paramètres moteur (`param_spec`), outils `ToolRegistry`.
- Tests passés (debug, local) : test_desktop_v2_studio_shell, test_desktop_v2_shell, garde de parité,
  display_filters, simulation_nav, thread_charts. CI #283 : voir `gh pr checks 283`.
- Prochain lot (le plus bloquant) : **images → régions → points** dans Studio (import image/SVG, prétraitement,
  segmentation, numérisation via `digitize_pipeline`/TaskManager, annulation, sauvegarde), puis souris
  (sélection rectangle, glisser-déplacer), inspecteur piloté par `param_spec`.
- Propriété des fichiers : Lead = CMake, matrice, CI ; Core = `apps/desktop_v2/core/` ; Studio = `apps/desktop_v2/studio/`.

## Mise à jour — vague 1 multi-agent (familles A fichiers, B édition, C inspecteur, D vue/analyse)
- Branches d'agents (poussées, sans PR) : `claude/studio-fam-a-fichiers` (a7eb47b), `-b-edition` (0d8df54),
  `-c-inspecteur` (4a2dab4) ; D (`-d-vue`) en cours. A+B+C fusionnées dans la branche locale de #283,
  PAS encore poussées : `test_desktop_v2_studio_edit` plante par intermittence (segfault, même
  adresse) — B diagnostique ; ne pousser qu'après 10 exécutions consécutives vertes.
- Chaque exécutable de test Studio embarque tous les QML par glob (`studio_all_qml`).
- Limites connues : param_spec ne couvre pas tout le moteur (types contour_fill, radial, crosshatch,
  motif, blanket, auto_satin ; champs handmade, seed, density_gradient, center_mm, pull_*, caps…) :
  étendre param_spec avant de prétendre à la parité de l'inspecteur.
- Prochaine vague : images/segmentation, outils vectoriels, satin/fondu/compensation, production, compagnon.
