---
name: openstitch-multi-agent
description: Décider s'il faut déléguer à des sous-agents sur OpenStitch, et comment (modèle, périmètre, worktree, intégration). À utiliser quand un chantier a au moins deux unités indépendantes et assez grosses pour amortir le coût de contextes séparés, quand une exploration large du code est nécessaire, ou quand l'utilisateur dit « multi-agent », « en parallèle », « délègue », « lance des agents ». Une tâche courte ou localisée se fait directement, sans agent.
---

# Délégation adaptative OpenStitch

Le multi-agents est un outil d'optimisation, pas un réflexe. Chaque sous-agent repart d'un
contexte vide (CLAUDE.md, outils, skills, prompt) et fait ses propres lectures : il ne
rapporte que s'il économise plus qu'il ne coûte, en temps (parallélisme) ou en contexte du
chef de lot (sorties volumineuses gardées hors de sa session). Aucun plafond arbitraire
sur le nombre d'agents : le nombre suit le nombre d'unités réellement indépendantes.

## 1. Décider (dans cet ordre)

| Situation | Décision |
|---|---|
| Tâche courte, un ou quelques fichiers, cause connue | **Direct**, pas d'agent |
| Question de localisation isolée (« où est X », « qui appelle Y ») | `os-explorer` (haiku), ou un Grep direct si la première recherche suffit |
| Sortie très volumineuse à digérer (journal de build, gros audit de lecture) | Agent économique, qui rend un résumé de ≤ 25 lignes |
| Unité de dev indépendante : fichiers disjoints, critère de fin vérifiable, > ~1 h de travail | `os-dev` (sonnet, worktree) |
| Deux unités ou plus remplissant la ligne précédente | Parallèle : un `os-dev` par unité, tous lancés dans un même message |
| Décision d'architecture, bug non élucidé après 2 hypothèses, arbitrage délicat | `os-architect` (opus), lecture seule, une question précise |
| Bug dont la cause est inconnue, ou changement qui traverse un point de convergence | **Direct** (séquentiel) : rien à découper tant que la cause n'est pas connue |

Choix du modèle : par invocation (`model:` de l'appel Agent) > frontmatter de l'agent >
`CLAUDE_CODE_SUBAGENT_MODEL` (posé à `sonnet` dans `.claude/settings.json`) > modèle de la
session. Haiku : recherche, résumé, tri. Sonnet : tout développement borné. Opus : seulement
si le raisonnement fin change le résultat, et le dire dans le prompt.

## 2. Découper

Une unité a : une **liste fermée de fichiers** (deux agents ne touchent jamais le même
fichier), un **critère de fin vérifiable** (cible de test qui passe), aucune dépendance de
résultat vers une unité en cours.

**Fichiers de convergence** (`app_theme.cpp`, `design_tokens.*`, `main_window.cpp`,
`CMakeLists.txt`, `docs/roadmap-parite-hatch.md`) : un seul propriétaire, le chef de lot.
Les agents demandent ce dont ils ont besoin dans leur rapport. Pour le câblage bureau, poser
d'abord des zones balisées `[cablage-X:debut/fin]` (hpp, constructeur, CMake, tests), les
commiter ; l'agent fait `git merge --ff-only <commit>` avant de commencer.

## 3. Le prompt d'un agent (autonome, court)

Il n'a pas la conversation. Donner, et seulement cela :
- l'objectif en 2-3 phrases et les **critères d'acceptation** (cibles de test exactes) ;
- le **périmètre STRICT** de fichiers, et ce qu'il ne doit pas toucher ;
- les **points d'entrée** : symboles, fichiers, plages de doc (`fichier.md:début-fin`) — pas de
  document en pièce jointe, pas de « lis tout le dépôt » ;
- ce qui est déjà établi (ne pas le lui faire re-découvrir) ;
- **validation limitée à ses cibles** : `scripts/qrun.ps1`, pas de suite complète ni de
  rebuild global (le chef de lot valide l'ensemble) ;
- contraintes d'environnement : clang-format 18.1.8
  (`%APPDATA%\Python\Python314\Scripts\clang-format.exe`), `QT_QPA_PLATFORM=offscreen`,
  `TEST_CASE` en ASCII, ne pas pousser ;
- le format du rapport : ≤ 20 lignes (fichiers, tests lancés + résultat, écarts), puis **s'arrêter**.

Commiter d'abord la base commune : un worktree part du dernier commit de `main`, jamais de
l'arbre non committé ni de la branche du chef de lot. Chaque worktree reconfigure son `build/`
(première compilation longue : le dire).

## 4. Pendant et après

Le chef de lot traite les fichiers de convergence ; il ne relit pas les journaux bruts des
agents et n'attend que la notification de fin. Intégration **séquentielle**, pour chaque agent :
1. rapport + `git diff <base>..<branche> --stat` : rien hors périmètre ;
2. `git merge --no-ff <branche>` ; un conflit n'est jamais résolu en silence ;
3. recompiler et relancer **soi-même** les cibles de l'agent et celles qu'il peut casser (la
   sortie de l'agent n'est pas une preuve) ;
4. agent en échec ou en débordement : `SendMessage` avec un prompt plus précis, pas de rustine.

Une fois tout intégré : une seule validation complète proportionnée à l'impact
(`openstitch-build-test` §2), format sur les fichiers touchés, contrôle visuel si l'IHM change.
Rapport à l'utilisateur : livré par chaque agent, vérifié par le chef de lot (comment), non
vérifié, écarts de périmètre.

## Pièges connus

- Le worktree d'un agent part de `main` : il ne voit pas le travail non fusionné. Soit il n'utilise
  que ce que `main` fournit, soit la base est fusionnée d'abord.
- Sous `ctest`, un test peut bloquer 300 s sans lien avec le changement (vu sur
  `satinJunctionGuideIsLockedInUi`) : relancer avec `--timeout 400` avant de conclure.
- Un `.exe` de test lancé sans le PATH de Qt sort en `0xC0000135` : passer par `ctest` ou ajouter
  `%QT_ROOT%\bin` au PATH.
- Captures d'écran : utiliser `PrintWindow` sur le handle du processus (l'appli peut ne pas être
  au premier plan).
- Deux agents qui ajoutent chacun une règle au même fichier QSS global se marchent dessus :
  feuilles de style locales par panneau, ou une seule main.
- Câblage bureau : sauf demande contraire, chaque unité inclut le câblage `apps/desktop`.
