# 07 — Journal d'avancement et problèmes ouverts

Reflète l'état **réel**. Chaque entrée : ce qui a été fait, la preuve, ce qui ne l'est pas. Ne jamais y écrire une
intention comme un résultat.

## 2026-11-04 — Session 1 : audit, architecture, fondation exécutable (branche `feat/desktop-v2-foundation`, PR V2-0)

### Fait, avec preuve

| Élément | Preuve |
|---|---|
| Audit de dette mesuré | `01-audit-dette.md` (chiffres tirés de grep/wc/CLI le jour même) |
| Inventaire et matrice de parité (44 familles) | `02` — ✅ 2 · 🟡 13 · ⬜ 29 |
| ADR 0003 (technologie) et 0004 (architecture) | `docs/adr/` |
| `core/` Qt-free : `SelectionModel`, `TaskManager`, `DocumentSession`, `ToolRegistry`, chaîne image → DST, aperçu | `apps/desktop_v2/core/` ; **32 cas Catch2** (`test_desktop_v2_core`) verts |
| `ui/` : shell, 4 espaces, rail d'outils, arbre documentaire, canevas d'aperçu, barre d'état avec progression/annulation | `apps/desktop_v2/ui/` ; **11 tests QTest headless** (`test_desktop_v2_shell`) verts |
| Parcours image → aperçu → renommage/masquage annulable → DST | tests `imageToDstEndToEnd`, `renamingFromTheTreeIsOneUndoStep`, `hidingFromTheTreeGoesThroughTheBatchPathAndUndoes` |
| Aller-retour `.osp` | test `projectSaveThenReopenRoundTrips` |
| Déterminisme | test : deux numérisations → DST identiques octet pour octet |
| Parité des 20 outils de canevas (déclarés) | test `every legacy canvas tool has a V2 equivalent` |
| Capture réelle (Windows, plateforme native, thème sombre) | `captures/shell-tentabrode-300mm.png` |
| Garde-fous du dépôt | `check_*` (CTest) verts ; clang-format 18.1.8 : `--dry-run --Werror` propre sur les fichiers ajoutés |
| Build Windows MSVC Debug | `cmake --build --preset msvc-debug` : 0 avertissement sur les cibles V2 |

Un bug a été trouvé **par les tests** pendant la session (et corrigé) : `TaskManager::submit` testait l'`optional`
au lieu du `Result`, donc une tâche en échec était comptée comme réussie ; les tests `task failure…` et `cancelled task…`
l'ont détecté. Une fuite de verrou Windows dans un test (flux ouvert pendant `remove`) a aussi été corrigée.

### Mesures

Tentabrode (`tests/fixtures/tentabrode.png`), hauteur d'import visée 300 mm, `max_colors = 6` : **139 objets, 43 562 points,
401 sauts, 372 coupes, 5 changements de couleur, emprise cousue 230,0 × 276,9 mm**, ≈ 1,5 s (Debug). Avec les réglages
par défaut du shell (`max_colors = 8`) : 138 objets, 43 670 points.

### Non fait (explicitement)

- Presque tout le périmètre fonctionnel : **29 familles sur 44 sont ⬜**, 13 partielles (`02`). Desktop V2 n'est pas
  utilisable comme desktop principal ; la bascule n'est pas à l'ordre du jour de cette PR.
- Aucun prototype QML, aucune mesure de fluidité Widgets/QML (ADR 0003 : décision conditionnelle, déclencheurs écrits).
- Pas de maquettes de parcours, pas de design system matérialisé (tokens, icônes), pas de mode tactile, pas d'anglais
  (chaînes V2 non extraites dans `openstitch_en.ts`), pas de test sous 1000 px.
- Pas de comparaison chiffrée avec l'ancien desktop (indicateurs `06` à remplir), pas de test utilisateur.
- Pas de validation sous Linux (CI `linux-core` à lire sur la PR) ni de recette sur une autre machine.
- Compagnon Claude : seule la primitive (`previewBatch`/`applyBatch`) existe, testée côté session ; aucun panneau.
- Sous-agents non utilisés : la fondation (modèles partagés + contrats) demandait un architecte unique ; la
  parallélisation devient rentable à partir de V2-3/V2-4 (périmètres disjoints).

### Problèmes ouverts

1. **« 300 mm »** — l'import à 300 mm donne un dessin cousu de 276,9 mm de haut (marge blanche non cousue). Si la cible
   est la hauteur du *motif*, il faut un mode « hauteur du motif » (recadrage sur l'emprise des objets) : décision à
   prendre avec l'utilisateur avant de s'en servir comme critère de recette.
2. `saveProject` et la reconstruction de la liste d'objets sont synchrones sur le fil d'interface (OK à 138 objets).
3. Un aperçu est relancé à **chaque** changement de révision, sans debounce ; le rendu n'étant pas interruptible en cours
   de calcul, une rafale d'éditions peut empiler des rendus inutiles (livraison périmée écartée, mais CPU consommé).
4. Écart catalogue : 55 opérations de lot vs ≈ 91 commandes. Chaque lot de migration doit fermer l'écart de sa famille.
5. `ShellWindow` détruit ses abonnements dans le destructeur mais `DocumentSession` est un membre : un futur composant
   qui survit à la fenêtre devra se désabonner (à généraliser par un petit RAII d'abonnement).
6. Le test de parité des outils inclut `apps/desktop/tools.hpp` (à retirer à la bascule).

### Prochaine action recommandée

Lot **V2-1 + V2-2** (infrastructure d'interface puis canevas), en commençant par le banc de mesure du calque
d'interaction (déclencheurs ADR 0003) et la sélection au canevas, qui débloquent tous les lots d'édition. Les lots
V2-3 et V2-4 sont ensuite parallélisables (sous-agents en worktrees, périmètres de fichiers disjoints).

## 2026-11-04 — Session 1 (suite) : tranche canevas + inspecteur (branche `feat/desktop-v2-canvas-inspector`, empilée sur #238)

### Fait, avec preuve

| Élément | Preuve |
|---|---|
| `HitIndex` (index à grille sur les brins cousus, dernier cousu = dessus, emprises, point d'échantillon) ; l'identifiant d'objet est encodé dans la « couleur » des segments pour réutiliser `build_thread_segments` sans dupliquer unités ni axe Y | `core/hit_index.*` ; tests `pick…`, `inRect…`, `preview carries a coherent mm/pixel frame…`, `hidden objects are neither rendered nor selectable…` |
| Aperçu avec repère mm↔pixels et index ; l'aperçu ne recalcule plus la séquence deux fois (moteur `stitch_render` appelé directement, mêmes paramètres que `render_realistic_png`) | `core/preview_service.*` |
| Sélection au clic sur le canevas (Ctrl = bascule, Maj = ajoute, vide = désélectionne, glisser = déplacer la vue) + cadres de sélection | tests QTest `clickingTheCanvasSelectsTheObjectUnderTheCursor`, `aDragOnTheCanvasPansInsteadOfSelecting` |
| Inspecteur contextuel : valeurs communes / mixtes ; type (6 types), couleur, visible, verrouillé ; **un lot = une annulation quel que soit le nombre d'objets** ; type refusé (satin) = rien d'empilé | `core/inspector_model.*`, `ui/inspector_panel.*` ; tests `inspector…` (Catch2 + QTest) |
| Aperçu regroupé (debounce 120 ms) : 6 modifications rapides → ≤ 2 rendus | test `burstsOfEditsRenderFewerPreviewsThanEdits` (ferme le problème ouvert n° 3) |
| Capture avec 3 objets sélectionnés | `captures/shell-selection-inspector.png` |
| Totaux locaux (Windows, Debug) | Catch2 : 41 cas / 527 assertions ; QTest : 17 fonctions ; 0 avertissement |

Un défaut de destruction trouvé par les tests (inspecteur détruit après la session) est corrigé.

### Non fait

Cadre élastique, poignées, outils de dessin, palette de commandes, centre de notifications, design system, i18n EN, RAII
d'abonnement (problème ouvert n° 5), banc de mesure Widgets/QML (déclencheurs ADR 0003 toujours non évalués).
Le sélecteur de couleur utilise `QColorDialog` (modal, comme un sélecteur de fichier) ; sa logique est testée sans dialogue.
Un objet **entièrement recouvert** par un autre n'a pas de point cliquable (seul le dernier cousu est retourné) :
il reste sélectionnable dans l'arbre.


## 2026-11-04 — Session 1 (suite 2) : cadre élastique et palette de commandes (branche `feat/desktop-v2-palette-rubberband`, empilée sur #239)

### Fait, avec preuve

| Élément | Preuve |
|---|---|
| Cadre élastique (outil Sélection) : sélection « fenêtre » = objets ENTIERS dans le cadre ; Ctrl/Maj ajoutent ; l'outil Déplacer la vue garde le panoramique | `HitIndex::inside`, `PreviewCanvas` (`QRubberBand` géré à la main : le cadre natif de `QGraphicsView` n'émettait rien sous QTest) ; tests `rubberBandSelectsWholeObjectsOnly`, `panToolDragsTheViewInsteadOfARubberBand`, `inside selects only objects fully within the rectangle` |
| Tout sélectionner (Ctrl+A) / Désélectionner (Ctrl+D) | test `selectAllAndClearThroughCommands` |
| Palette de commandes (Ctrl+K) : `CommandEntry` + `filterCommands` Qt-free (insensible à la casse et aux accents, tous les mots, rang libellé > reste, activées d'abord, ordre stable) ; chaque commande a un identifiant stable (`tool.*`, `workspace.*`, `file.*`, `edit.*`, `selection.*`, `view.*`) exécuté par `ShellWindow::runCommand` | tests `palette filter…`, `commandsCoverToolsWorkspacesAndEditing` (20 outils atteignables, un outil non migré est listé mais refusé), `paletteFiltersAndRunsTheChosenCommand` |
| Un outil absent de l'espace courant est atteignable : la commande bascule vers le premier espace qui le montre (principe B) | `runCommand("tool.…")` |
| Totaux locaux (Windows, Debug) | Catch2 : 46 cas / 546 assertions ; QTest : 22 fonctions ; 0 avertissement |

### Constat

Sur Tentabrode à 80 mm, **12 objets sur 90 n'ont aucun point cousu** (le cadre autour de toute l'image en sélectionne 78) :
ils existent dans le document mais n'ont pas d'emprise au canevas. Ils restent sélectionnables dans l'arbre. À
expliquer côté moteur (objets de séparation vides ? objets entièrement recouverts ?) avant d'afficher un indicateur.

### Non fait

Poignées de nœuds, outils de dessin, design system, i18n EN, centre de notifications, banc de mesure Widgets/QML.


## 2026-11-04 — Session 1 (suite 3) : outils de dessin (branche `feat/desktop-v2-draw-shapes`, empilée sur #240)

### Fait, avec preuve

| Élément | Preuve |
|---|---|
| Nouveau projet (Ctrl+N) à cadre par défaut 100 × 100 mm ; l'aperçu montre **toujours le cadre de broderie** (fenêtre = cadre ∪ points) et fonctionne sur un document sans point | `file.new`, `render_preview` ; tests `a document without stitches still has a preview of its hoop`, `drawingTools…` |
| Outils **rectangle**, **ellipse** (Maj = cercle), **polygone** (clics, Entrée / double-clic termine, Retour arrière retire un sommet, Échap annule) : chaque forme = un lot `add_shape` = une annulation ; l'objet créé est sélectionné ; changer d'outil abandonne un polygone en cours | `core/shape_batch.*` (limites du catalogue vérifiées avant l'envoi, nom échappé), `PreviewCanvas::Mode` ; tests `a drawn rectangle becomes one object… at the right place` (emprise réelle contrôlée, axe Y retourné), `ellipse and polygon creation…`, `polygonToolClicksThenEnterAndEscapeCancels` |
| Forme refusée (polygone en papillon) : notice d'erreur, document et historique inchangés | tests `aRefusedShapeShowsAnError…`, `ellipse and polygon creation…` |
| Capture | `captures/shell-draw-shapes.png` |
| Totaux locaux (Windows, Debug) | Catch2 : 52 cas / 586 assertions ; QTest : 27 fonctions ; 0 avertissement |

Un défaut trouvé par les tests : `effective_sequence` refuse un projet **sans objet** (« Aucun objet de broderie ») ; l'aperçu
traite désormais ce cas lui-même (cadre seul). L'identifiant d'un objet créé par `add_shape` est dans `{"id":N,…}` (et
`{"created":[…]}` pour duplication/découpe) : `createdObjectId` lit les deux.

### Non fait

Bézier, main levée, polygone régulier, édition de nœuds / poignées, déplacement et mise à l'échelle à la souris (les ops
`translate`, `scale`, `set_geometry` existent), taille du cadre (`set_canvas`), design system, i18n EN, banc de mesure.


## 2026-11-04 — Session 1 (suite 4) : déplacer et mettre à l'échelle à la souris (branche `feat/desktop-v2-move-scale`)

### Fait, avec preuve

| Élément | Preuve |
|---|---|
| Déplacer : appui sur un objet (sélectionné ou non) puis glisser ; la sélection suit en direct (cadres décalés), au relâchement un lot `translate` (une annulation pour toute la sélection) ; un simple clic ne déplace rien | `core/transform_batch.*` (limites du catalogue, déplacement/échelle nuls refusés) ; tests `draggingAnObjectMovesTheSelectionInOneUndoStep`, `aPlainClickOnAnObjectDoesNotMoveIt`, `a move is one undo step for the whole selection and lands where asked` (axe Y contrôlé sur le rendu) |
| Mettre à l'échelle : 4 poignées aux coins de l'emprise commune (taille constante à l'écran), pivot = coin opposé, facteur uniforme, contour de prévisualisation pendant le glisser, un lot `scale` | tests `draggingACornerHandleScalesAboutTheOppositeCorner` (taille ×, coin opposé fixe, rapport d'aspect conservé), `a scale about a pivot keeps the pivot fixed` |
| Un glisser sur le tissu vide reste un cadre élastique | tests `aDragOnEmptyFabricIsARubberBandNotAClick`, `rubberBandSelectsWholeObjectsOnly` |
| Totaux locaux (Windows, Debug) | Catch2 : 56 cas / 631 assertions ; QTest : 31 lignes ; 0 avertissement |

### Intégration

Sur demande de l'utilisateur, **plus rien n'est fusionné dans `main`** : les PR V2 sont fusionnées dans la branche
d'intégration `integration/desktop-v2` (créée depuis `main`). #238 y est fusionnée ; #239–#241 y sont retargetées. Un
défaut de portabilité Linux (liste d'initialisation de `uint64_t` mixte avec `unsigned long long`) détecté par la CI
`linux-core` a été corrigé.

### Non fait

Rotation, miroir, alignement, flèches clavier, Bézier, main levée, nœuds, taille du cadre, design system, i18n EN.


## 2026-11-04 — Session 1 (suite 5) : opérations d'édition (branche `feat/desktop-v2-edit-ops`)

### Fait, avec preuve

| Élément | Preuve |
|---|---|
| Supprimer (Suppr), dupliquer (Ctrl+D, les copies deviennent la sélection), pivoter ±90°, miroir horizontal / vertical (pivot = centre de l'emprise de la sélection), ordre de couture (4 commandes), flèches (1 mm ; Maj = 10 mm) : tout passe par des lots `project_ops` (`remove_object`, `duplicate_object`, `rotate`, `mirror`, `stack_order`, `translate`), une annulation chacun | `core/edit_batch.*` ; 8 cas Catch2 (rotation 90° échange largeur/hauteur, centre inchangé ; miroir ; copies décalées et identifiants rapportés ; ordre) ; QTest `editingCommandsActOnTheSelectionOnly`, `rotateAndMirrorUseTheSelectionCentre`, `arrowKeysNudgeTheSelectionByOneOrTenMillimetres` |
| Menu Édition : Supprimer, Dupliquer, Transformer, Ordre de couture ; palette : 10 commandes de plus, désactivées sans sélection | `commandEntries` |
| Raccourci « Désélectionner » déplacé de Ctrl+D à **Ctrl+Maj+A** (Ctrl+D = dupliquer, convention) | menu, palette |
| Totaux locaux (Windows, Debug) | Catch2 : 64 cas ; QTest : 34 lignes ; 0 avertissement |

### Non fait

Angle de rotation libre et poignée de rotation, alignement / répartition, groupes, Bézier, main levée, nœuds, taille du
cadre, design system, i18n EN.
