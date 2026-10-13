# Premier lot de correctifs UX/UI — 2026-10-13

*État : correctifs intégrés dans un worktree dédié ; suite Release complète et
contrôle visuel ciblé réalisés, validation Debug en cours.*

## Périmètre et référence

Suite de l'[audit gelé sur `fb00455`](ui-ux-audit-2026-10-13.md), sans réécrire ses
preuves historiques. Branche `fix/ux-ui-correctifs-20261013`, worktree
`C:\Users\zache\.local\share\opencode\worktree\85b307\ux-ui-correctifs`.
L'arbre principal n'est pas utilisé pour les modifications ou les builds.

Deux unités indépendantes ont livré les masques IA et les interactions canevas
dans des worktrees séparés, avec tests et correctifs séparés. La session
d'intégration a relu les diffs, appliqué d'abord les tests pour constater les
échecs, puis les correctifs. Tous les builds et tests ont été exécutés dans le
worktree d'intégration, sans builds concurrents.

`mesh-mcp/search_docs` indisponible : recherches et lectures documentaires ciblées.
Aucun changement de format `.osp`, de paramètre persistant, de générateur de
points ou de dépendance. Livraison sur une branche dédiée, sans fusion vers `main`.

## Causes racines et corrections

| Sujet | Cause vérifiée | Correction et preuve ciblée |
|---|---|---|
| Build préalable | La lambda de `offsetVectorObjectCore` utilisait la constante locale `kFlattenTolerance` sans capture ; MSVC refusait l'application. | Capture explicite ; bibliothèque desktop et cibles de tests compilées en Release. |
| UX-02 — document sans raster | `refreshImage` retournait avant génération, invalidation d'analyse et rafraîchissement des panneaux. Une ancienne séquence pouvait aussi survivre à l'undo d'un import machine. | Seule l'étape raster est conditionnelle ; régénération via `refresh_context` pour tous les contenus. Tests `vectorOnlySequenceTracksEditsDeletionAndUndoRedo`, `svgOnlyCreationGeneratesAnExportableSequence`, `importedOnlyUndoClearsThePreviewAndExportActions` : rouge puis vert. |
| UX-08 — import machine seul | Accueil et sauvegarde réelle reconnaissaient `imported_design`, mais pas toutes les activations ni l'autosave. | `hasDocumentContent` partagé, affichage des points piloté par la séquence effective. Tests `importedOnlyDocumentEnablesSaveAndStitchVisibility`, `importedOnlyModifiedDocumentIsAutosavedAndReloadable` : rouge puis vert, relecture des `.osp` incluse. |
| UX-06 — type déjà actif | Le choix actif repassait par les paramètres par défaut et une conversion de groupe, même sans changement de type. | Aucun effet si toutes les sections ont déjà le type demandé et qu'aucun réglage explicite n'est appliqué. Tests `selectingTheCurrentStitchTypeIsANoOp`, `selectingTheCurrentGroupStitchTypePreservesEverySection` : rouge puis vert. |
| UX-35 — identités IA | Les cellules numériques étaient éditables ; aperçu, fusion et validation relisaient l'identifiant depuis le texte affiché. | Lecture seule et identité dans `Qt::UserRole`, vérifiée dans les trois parcours, y compris après tri et pour une identité absente/inconnue. Six fonctions QTest dédiées dans `test_segmentation_workflow`. |
| UX-36 — protection IA | La fusion recréait sa case de protection décochée après suppression des sources. | Protection héritée par OU logique. Quatre combinaisons de sources testées ; le cas sans source protégée était déjà vert avant correction. |
| UX-37 — sélection survolée | La règle `:hover` remplaçait le fond de sélection, mais gardait son texte. | Survol limité à `:!selected` pour les listes. Test de contrat QSS dans les deux thèmes : rouge puis vert, sans comparaison de pixels. |
| UX-38 — grille sombre | La grille blanche translucide était peinte sur le tissu blanc. | Ton `#6f6ba8`, discernable sur tissu blanc et extérieur sombre ; test des couleurs composées, pas d'une capture. Contraste majeur calculé : 4,834:1 sur blanc et 3,960:1 sur le fond sombre. |
| UX-39 — poignées | Qt préfiltre les cibles par leur boîte englobante ; le carré hérité excluait la cible 22 DIP. L'ellipse Qt 6.8.3 n'échouait qu'avec stylo nul/absent. | Boîte englobant cible et dessin, visuel 8 × 8 inchangé. Requêtes scène/vue, clics et glissers aux bords ±10 DIP, zooms 0,2/4/400, index BSP/sans index et chevauchement. |
| UX-40 — touche Espace | Le filtre protégeait seulement les champs de saisie, pas les boutons/cases/listes focalisés hors canevas. | Priorité au widget focalisé, sans vol de focus au survol ; tests d'activation par Tab/Espace et retour au panoramique. L'ancien test qui attendait la capture d'Espace par le canevas est remplacé : il figeait le défaut. |
| UX-41 — focus primaire | La bordure `[primary="true"]` écrasait la bordure de focus générique. | Règle `primary:focus` explicite après les règles primaires ; contrat QSS vérifié en clair et sombre. |

## Hypothèses écartées

- Ajouter un raster fictif aurait masqué UX-02 sans unifier le vrai chemin de
  rafraîchissement. Le cache raster existant conserve sa clé de contenu exacte.
- Activer seulement les menus de sauvegarde aurait laissé l'autosave et la
  visibilité des points incohérentes. Le contenu reste lu dans le document,
  jamais déduit d'une copie d'affichage pour les gardes de sauvegarde.
- Désactiver seulement l'édition du tableau IA ne séparerait pas l'identité de
  l'affichage ; protéger toutes les fusions changerait le cas non protégé.
- Agrandir le dessin des poignées, multiplier la cible par zoom/DPR ou voler le
  focus au survol ne corrigerait pas les contrats Qt concernés.
- Augmenter l'opacité d'une grille blanche sur blanc garderait le contraste 1:1.

## Validation exécutée et limites

- Documents/type actif : **7 scénarios ciblés** rouges avant correction, verts
  après ; contrat de thème : **2 scénarios** rouges puis verts.
- Masques IA : **14 cas en échec avant**, puis **15 cas fonctionnels verts**,
  dont le cas non protégé déjà vert sur la référence (17 passes avec init/cleanup).
- Poignées : **128 cas en échec avant**, puis suite complète **166 passes**.
- Clavier/canevas ciblé : **28 cas en échec avant**. Suite complète après
  correction : **118 passes, 1 skip Windows** (`pixelDelta` non livré).
- Ces deux suites canevas passent aussi avec `QT_SCALE_FACTOR=2` ; tests Qt
  headless, sans attente fixe ni comparaison de pixels.
- Les mesures sRGB ne prouvent pas le rendu final, ni une conformité WCAG
  globale. Le test de focus prouve une règle appliquée, pas une validation NVDA.
- Aucun gain de vitesse annoncé ; génération, validation IA et autres calculs
  lourds restent synchrones dans les chemins recensés par UX-04.

### Non-régression et livrables

- Build **Release complet**, application/CLI/tests inclus : réussi.
- **CTest Release : 1087/1087**, dont les **21 suites desktop**, garde
  `check_no_raw_sequence_bypass` et synchronisation des gestes documentés.
- Première exécution : 1084/1087, trois contrats CLI échouant sur les accents
  (`cli_errors_prefix`, `cli_info_json`, `cli_digitize_json`). Sans changement
  de code, passage en console UTF-8 : 11/11 contrats CLI, puis 1087/1087 lors
  d'un nouveau passage complet. Il s'agit de l'encodage de capture du runner
  Windows/CMake, pas d'un correctif CLI dans ce lot.
- **clang-format 18.1.8 : 450 fichiers C++ conformes** ; `git diff --check` OK.
- Guide utilisateur actualisé ; **PDF régénéré : 261 pages, 45 chapitres,
  0 problème et 0 avertissement**.
- **Quatre captures natives Qt**, 1400 × 900 DIP, densité confortable : accueil
  et document vectoriel sans raster, en clair et sombre. Programme de contrôle
  temporaire lié aux bibliothèques Release, paramètres et projet synthétique
  isolés (pas de modification du profil utilisateur).
  Fichiers locaux : `build/ux-captures/welcome-light.png`, `welcome-dark.png`,
  `vector-light.png`, `vector-dark.png`. Grille discernable dedans/dehors,
  points présents et export actif sur le carré sans raster, contrôlés à la
  lecture des captures. Ce n'est pas une matrice complète petits écrans/DPI,
  ni une validation visuelle du survol de toutes les listes ou de NVDA.

Commandes de non-régression (racine du worktree) :

```powershell
cmake --build --preset msvc-release --parallel 2
cmd /d /c "chcp 65001 > nul && ctest --preset msvc-release --output-on-failure --timeout 400 -j 1"
powershell -NoProfile -File docs/scripts/build-docs.ps1
```

Debug en cours. Les journaux locaux sont sous `build/msvc/ux-*`, non versionnés.

## Reste hors de ce lot

UX-04 (asynchronisme/annulation), UX-07 (hauteur d'import), UX-10 (pré-export),
UX-13/14 (Document/workflow), UX-15/16 (repli/annulation IA), UX-33/34
(états mixtes/portée de groupe), et les autres constats de l'audit non listés
ci-dessus. Ce premier lot n'est pas une résolution des 32 constats.
