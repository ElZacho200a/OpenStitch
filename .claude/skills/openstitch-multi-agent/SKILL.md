---
name: openstitch-multi-agent
description: Travailler en multi-agents sur OpenStitch (sous-agents en worktrees isolés, périmètres de fichiers disjoints, intégration séquentielle par le chef de lot). À utiliser dès qu'un chantier se découpe en au moins deux unités indépendantes touchant des fichiers différents — refonte d'IHM panneau par panneau, plusieurs lots de la roadmap, audit ou revue en parallèle, exploration large du code — ou quand l'utilisateur dit « multi-agent », « en parallèle », « délègue », « lance des agents ». Préférer cette méthode au travail séquentiel dès que le découpage est propre.
---

# Travail multi-agents OpenStitch

L'utilisateur préfère cette façon de travailler quand le chantier s'y prête :
plusieurs agents en parallèle, un chef de lot (la session principale) qui cadre,
intègre et vérifie. Ce n'est PAS une dispense de rigueur : un agent qui dit
« fini » n'a rien prouvé tant que le chef de lot n'a pas relu et relancé les tests.

## 1. Quand découper

Découper seulement si **chaque unité** a :
- un **périmètre de fichiers disjoint** (liste fermée, ex. `document_panel.*` +
  son test) — deux agents ne modifient jamais le même fichier ;
- un **critère de fin vérifiable** (cible de test précise qui passe) ;
- aucune dépendance de résultat vers une autre unité en cours.

Ne pas découper : un changement qui traverse `design_tokens` / `app_theme` /
`main_window.cpp` (points de convergence), un bug dont la cause n'est pas connue,
une tâche de moins d'une heure à un seul fichier.

**Fichiers de convergence** (`app_theme.cpp`, `design_tokens.*`, `main_window.cpp`,
`CMakeLists.txt`, `docs/roadmap-parite-hatch.md`) : un seul propriétaire, le chef
de lot. Les agents n'y touchent pas ; ils demandent ce dont ils ont besoin dans
leur rapport (ou utilisent une feuille de style locale au widget, bâtie depuis les
jetons et rafraîchie sur `AppTheme::changed`).

## 2. Lancer

- Un agent par unité, `isolation: "worktree"`, en arrière-plan, **tous dans un
  même message** pour qu'ils tournent en parallèle.
- **Committer d'abord** la base commune (jetons, icônes, QSS globaux) : un worktree
  part du dernier commit, jamais de l'arbre de travail non committé.
- Le prompt est autonome (l'agent n'a pas la conversation) : contexte, périmètre
  STRICT de fichiers, ce qu'il ne doit pas toucher, contraintes du dépôt
  (`CLAUDE.md`, `GUARDRAILS.md`), validation obligatoire avec les cibles exactes,
  format de commit et de rapport. Toujours préciser : **clang-format 18.1.8**
  (`%APPDATA%\Python\Python314\Scripts\clang-format.exe`, jamais celui de LLVM 23),
  `QT_QPA_PLATFORM=offscreen`, noms de `TEST_CASE` en ASCII, **ne pas pousser**.
- Les agents partagent les chemins vcpkg/Qt par variables d'environnement ; chaque
  worktree reconfigure son propre `build/` (la configuration est rapide grâce au
  cache vcpkg, la première compilation est longue : prévoir le temps).

**Câblage bureau.** Sauf demande contraire, chaque unité confiée à un agent inclut le
câblage dans `apps/desktop` (pas seulement le cœur). Si le périmètre de fichiers
interdit `main_window.*`, poser d'abord des zones balisées `[cablage-X:debut/fin]`
(hpp, constructeur, CMake, tests), les commiter, et demander à chaque agent de
`git merge --ff-only <commit>` avant de commencer (un worktree part de `main`).

## 3. Pendant que les agents travaillent

Le chef de lot traite les fichiers de convergence et ce que personne d'autre ne
touche. Il ne refait pas le travail d'un agent et ne lit pas les journaux bruts
des agents (ils saturent le contexte) : il attend la notification de fin.

## 4. Intégrer (séquentiel, jamais en parallèle)

Pour chaque agent, dans l'ordre :
1. Lire son rapport **et le diff** (`git diff <base>..<branche-agent> --stat`
   puis les fichiers) : vérifier que rien n'est sorti du périmètre.
2. `git merge --no-ff <branche-agent>` dans la branche d'intégration (ou
   cherry-pick). Un conflit n'est jamais résolu automatiquement : le montrer.
3. Recompiler et relancer les tests **soi-même** (cibles de l'agent + celles que
   sa modification peut casser). La sortie de l'agent n'est pas une preuve.
4. Si l'agent a échoué ou débordé : le corriger ou le relancer avec un prompt
   plus précis (`SendMessage`), pas de rustine silencieuse.

Après toutes les intégrations : suite desktop complète (`ctest --preset
msvc-debug -R desktop`), `clang-format --dry-run --Werror` sur les fichiers
touchés, contrôle visuel de l'application (capture des deux thèmes).

## 5. Rapport à l'utilisateur

Dire ce que chaque agent a livré, ce qui a été vérifié par le chef de lot (et
comment), ce qui ne l'a pas été, et tout écart de périmètre constaté.

## Pièges connus

- **Le worktree d'un agent part de `main`, pas de la branche du chef de lot** (constaté
  lors de la refonte UI : l'agent ne voyait ni les nouveaux jetons ni les règles QSS).
  Conséquence : un agent ne peut pas s'appuyer sur du travail non fusionné dans `main`.
  Soit lui demander de n'utiliser que ce que `main` fournit (il dérive localement ce
  qui manque, et le chef de lot remplace après fusion), soit fusionner d'abord la base
  dans `main`. Les périmètres de fichiers disjoints gardent la fusion propre.
- Sous `ctest`, un test peut bloquer 300 s sans cause liée au changement (vu une fois
  sur `satinJunctionGuideIsLockedInUi`) : relancer avec `--timeout 400` avant de conclure.
- Lancer un `.exe` de test directement sans le PATH de Qt sort en `0xC0000135` : passer
  par `ctest` ou ajouter `%QT_ROOT%in` au PATH.

- Les captures d'écran du bureau capturent la mauvaise fenêtre si l'appli n'est
  pas au premier plan : utiliser `PrintWindow` sur le handle du processus.
- Deux agents qui ajoutent chacun une règle au même fichier QSS global se
  marchent dessus : feuilles de style locales par panneau, ou une seule main.
- Un worktree d'agent sans `build/` doit se reconfigurer : l'indiquer dans le prompt.
