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


## 2026-11-04 — Session 1 (suite 6) : diagnostic de brodabilité (branche `feat/desktop-v2-diagnostics`)

### Fait, avec preuve

| Élément | Preuve |
|---|---|
| Diagnostic : `project_ops::diagnose_project` (même contrôle que la CLI et l'agent) exécuté sur un instantané, en tâche annulable ; le résultat porte la révision analysée | `core/diagnostics_service.*` ; tests Catch2 `diagnostics of a digitized document…`, `diagnostics refuse an empty document and honour cancellation` |
| Panneau « Diagnostic » (onglet du dock de droite, mis en avant dans l'espace Optimisation) : synthèse (erreurs / avertissements / infos, points, sauts, coupes, couleurs, fil, emprise), constats colorés, **double-clic = sélection des objets concernés + centrage de la vue** ; objets disparus ignorés ; résultat marqué **périmé** dès qu'une modification arrive | `ui/diagnostics_panel.*` ; QTest `diagnosticsRunInTheBackgroundAndGoStaleOnEdit`, `clickingAFindingSelectsItsObjectsAndSkipsVanishedOnes` |
| Commande de palette « Analyser le document » (bascule vers l'espace Optimisation) ; désactivée sans objet | `analysis.run` |
| Totaux locaux (Windows, Debug) | Catch2 : 64 cas / 710 assertions ; QTest : 36 lignes ; 0 avertissement |

### Non fait

Correctifs proposés en un clic (les constats portent une suggestion textuelle), cartes de densité et de risque tissu en
surimpression, mesure de fidélité à l'image source, compensation de tirage, design system, i18n EN.


## 2026-11-04 — Session 1 (suite 7) : export multi-formats (branche `feat/desktop-v2-diagnostics`)

| Élément | Preuve |
|---|---|
| Export machine par extension (liste tirée de `formats::registered_formats()`, aucun format codé en dur) : séquence effective, couleurs de blocs (`stitch_analysis::color_blocks`), nom de motif ASCII 16 caractères, écriture atomique, annulable ; extension inconnue = erreur actionnable | `export_project_file`, `exportFormats` ; tests `every writable format exports Tentabrode to a non-empty readable file` (relecture, et **nombre de blocs de couleur identique** pour les formats qui portent les couleurs), `an unsupported or missing extension is refused…`, `the export list comes from the format registry…` |
| Sélecteur de fichier : un filtre par format écrivable | `ShellWindow` |

Non fait : options machine (point de départ, coupes, arrêts, horodatage), contrôle des contraintes machine à l'export,
fiche de production, impression.


## 2026-11-04 — Session 1 (suite 8) : réglages de couture dans l'inspecteur (branche `feat/desktop-v2-params`)

| Élément | Preuve |
|---|---|
| Table `ParamSpec` (type de point → réglages essentiels : libellé, nature, plage, pas, unité) ; lecture dans l'unité d'affichage (µm → mm, radians → degrés), écriture par `set_params` (mise à jour partielle) ; valeur hors plage, clé étrangère au type, non finie : refusée **avant** envoi | `core/param_spec.*` ; 4 cas Catch2 (chaque clé de la table est lisible sur son type et sa valeur par défaut est dans la plage ; une annulation pour N objets ; entiers arrondis) |
| Éditeur généré depuis la table dans l'inspecteur : numérique, entier, case ; valeur **mixte** affichée « — » quand les objets diffèrent ; champs reconstruits seulement si le type commun change | `ui/inspector_panel.*` ; QTest `theInspectorShowsEssentialStitchSettingsOfTheCommonType` |
| **Défaut d'ergonomie trouvé par les tests** : l'inspecteur plus haut agrandissait la fenêtre et redimensionnait le canevas **en plein clic** (la vue sautait, le clic tombait à côté). L'inspecteur est maintenant dans une zone défilante : il ne modifie plus la taille minimale de la fenêtre | trois tests de canevas échouaient, verts après correction |

Non fait : les ≈ 150 autres champs du moteur (la table est extensible sans toucher l'interface), réglages par objet vectoriel,
préréglages de points.


## 2026-11-04 — Session 1 (suite 9) : catalogue d'actions et 6 familles (branche `feat/desktop-v2-families-1`)

Nouveau mécanisme : `core/action_catalog.*`. Chaque capacité exposée par une opération de lot est déclarée **une fois**
(champs, plages, défauts, validation, construction du lot) ; l'interface en tire un dialogue, une entrée du menu
**Outils** et une entrée de palette ; aucune valeur n'est « corrigée » en silence (`buildAction` refuse). Ajouter une
famille = une définition + un test.

| Action | Opération moteur | État |
|---|---|---|
| Importer un SVG | `import_svg` | ✅ (test : SVG réel → objets ; fichier absent refusé avant tout lot) |
| Ajouter du texte | `add_text` | ✅ (police, hauteur, remplissage, alignement, rotation) |
| Cadre de broderie / Machine | `set_canvas`, `set_machine` | ✅ |
| Finitions (coupes, arrêt, ponts cachés) | `set_finishing` | ✅ |
| Satin automatique / Guides de direction | `auto_satin`, `directional_guides` | ✅ |
| Réduire le bruit / Segmenter l'image | `image_denoise`, `segment_image` | ✅ sur Tentabrode |

Preuves locales : 7 cas Catch2 `[actions]` (159 assertions : identifiants uniques, défauts valides, validations, chaque action
acceptée par le moteur), 4 QTest (dialogues générés → valeurs acceptées, menu + palette, refus avec message sans changement,
texte). Limite connue : la police par défaut du texte est cherchée via `OPENSTITCH_FONTS_DIR`, un chemin de développement
compilé, puis `resources/fonts` ; une application installée devra embarquer ses polices (champ « Police » modifiable).


## 2026-11-04 — Session 1 (suite 10) : affichage, règle, simulation, contexte (branche `feat/desktop-v2-families-2`)

| Famille | Fait | Preuve |
|---|---|---|
| 38 Règle / grille | outil Règle (Mode::Measure) : distance, Δx, Δy, angle antihoraire, **sans jamais modifier le document** ; grille 5 / 10 / 20 mm alignée sur l'origine du modèle (rien n'est tracé si trop dense) | `core/view_tools.*` (4 cas Catch2 dont angle ±90°/180° et traits sur les multiples) ; QTest `theRulerMeasuresADragInMillimetres`, `gridAndHoopToggles` |
| 29 Simulation | curseur sur la séquence de couture + lecture automatique, dans l'espace Production ; `render_preview(…, upToCommand)` : le **cadre et l'index restent ceux de la séquence complète**, la vue ne bouge pas ; quitter Production termine la simulation ; le document n'est jamais modifié | test Catch2 `simulation renders a prefix…` ; QTest `simulationScrubsThroughTheSewingOrderOnlyInProduction` |
| 40 Menu contextuel | clic droit : commandes de sélection actives seulement (pas de redite d'Annuler/Rétablir) ; un objet non sélectionné le devient d'abord | QTest `contextMenuListsOnlyActiveSelectionCommands`, `rightClickOnAnObjectSelectsItBeforeTheMenuOpens` |
| 43 Affichage | cadre de broderie et grille commutables | menu Affichage |
| 6 Export image | PNG de l'aperçu réaliste à 1–20 px/mm, écriture atomique, annulable | Catch2 (signature PNG, résolutions refusées, annulation) ; QTest `exportImageWritesAPng` |
| 36 Thème | système / clair / sombre (`QStyleHints::setColorScheme`) ; valeur invalide → système | QTest `themeModeIsValidatedAndRestorable` |

Non fait : mémorisation des réglages d'affichage, langue anglaise, PDF / impression, magnétisme, passes de couture.


## 2026-11-04 — Session 1 (suite 11) : géométrie, ordre, sous-couches, tirage prédictif, remplissages spéciaux (branche `feat/desktop-v2-families-3`)

| Famille | Fait | Preuve |
|---|---|---|
| 13 Géométrie | 10 actions de plus au catalogue : fusion (4 modes), simplifier, lisser, combler les trous, supprimer les miettes, séparer, numériser les formes, optimiser l'ordre (5 stratégies + 2-opt), coupe après l'objet, entrée / sortie automatiques, réduire la palette | Catch2 sur de vraies géométries : union de deux rectangles, **soustraction → trou → comblé**, union avec miette → miette supprimée, ellipse lissée ; une action **sans effet possible est refusée par le moteur et rien n'est empilé** |
| 24 / 25 | `ParamSpec` passe à la nature **Choice** : mode de compensation du tirage manuel / auto / adaptatif / **prédictif**, mode de sous-couche, zigzag, retrait, écart, liaisons cachées | tests Catch2 `choice settings write the engine keyword…` (l'index 3 écrit `predicted`, relu = 3 ; index hors plage et « mixte » refusés) |
| 21 | réglages de la spirale, du point de croix, de l'appliqué | tests `special fills expose their own essential settings` ; chaque clé de la table est lisible sur son type et sa valeur par défaut est dans la plage, pour 6 types |

**Incident de session** : un correctif de `readParam` n'avait pas été appliqué (remplacement textuel silencieux sur du code
reformaté) ; le test lisait un `optional` vide et se figeait. Corrigé en réécrivant la fonction ; les scripts de patch
de cette session vérifient désormais chaque remplacement.


## 2026-11-04 — Session 1 (suite 12) : calques et préréglages (branche `feat/desktop-v2-families-4`)

| Famille | Fait | Preuve |
|---|---|---|
| 42 Préréglages | `core/document_services` : enregistrer (nom nettoyé, remplace un homonyme), appliquer à N objets, supprimer — chacun **une `ICommand`** annulable ; menu « Préréglages » (reconstruit à l'ouverture), palette, menu contextuel | Catch2 (réglages relus sur l'objet cible, annulation, refus avec raison : nom vide / trop long / objet inconnu / indice hors plage) ; QTest `presetsAreSavedAppliedAndListedInTheMenuAndPalette` |
| 14 Calques | panneau « Calques » (onglet du dock) : liste du dessus vers le fond avec case de visibilité, ajout, renommage, suppression (le dernier reste), monter / descendre, verrou, « sélection ici » ; une annulation par action | Catch2 `calques: add, rename, hide, lock…` ; QTest `calquePanelEditsLayersThroughUndoableCommands`, `hidingACalqueHidesItsObjectsFromThePreview` |

**Défaut trouvé par les tests** : quand tous les objets étaient masqués (ou dans des calques masqués), le moteur refuse de
générer (« Aucun objet de broderie visible ») et l'aperçu **gardait l'ancienne image** avec son index de sélection (on pouvait
cliquer un objet invisible). L'aperçu montre désormais le cadre vide dans ce cas.
`DocumentSession::allocateObjectId()` est la seule écriture hors commande (compteur d'identifiants, jamais réutilisés).


## 2026-11-04 — Session 1 (suite 13) : fondu et pipette (branche `feat/desktop-v2-families-5`)

| Famille | Fait | Preuve |
|---|---|---|
| 22 Fondu de deux fils | `core/fade_service` : créer, éditer, supprimer un fondu (`project_ops::gradient_fill`, une `ICommand` pour les deux couches) ; le moteur refuse un remplissage incompatible et son message est relayé tel quel ; actions « Fondu de deux fils… » / « Supprimer le fondu » (menu Outils, palette) | Catch2 `a fade is created, edited and removed…`, `fade requests that cannot work are refused…` ; QTest `aFadeIsCreatedFromTheToolsMenuActionAndUndone` |
| 39 Pipette | outil Pipette (`Mode::Pick`) : le fil de l'objet cousu sous le curseur est appliqué à la sélection (une annulation) ; la sélection de l'utilisateur ne change pas | QTest `theEyedropperCopiesAThreadColourOntoTheSelection` |

Mécanisme : `ActionDef` accepte désormais `run` (commande documentaire) en plus de `build` (lot JSON) ; `validateAction`
valide les champs sans rien exécuter. `DocumentSession::projectForIdAllocation()` sert aux fabriques du moteur qui
réservent des identifiants (jamais autre chose). Avertissements MSVC `getenv`/`sscanf` supprimés (0 avertissement).


## 2026-11-04 — Session 1 (suite 14) : couteau (branche `feat/desktop-v2-families-5`)

Outil Couteau (`Mode::Cut`) : ligne tracée au glisser, `split` sur les formes sélectionnées (chaque morceau devient un objet, une
annulation) ; sans sélection : message ; ligne qui ne traverse rien : refusée par le moteur, rien n'est empilé. Preuves :
Catch2 `a knife line splits a shape…`, QTest `theKnifeCutsTheSelectedShapeAlongTheDraggedLine`.

**Bilan de la demande « terminer les 25 familles »** : 21 familles sont passées de ⬜ à 🟡 pendant cette session (état :
✅ 2 · 🟡 38 · ⬜ 4). Quatre restent ⬜ : une exception justifiée (IA, famille 9) et trois qui exigent des outils de canevas
interactifs adossés à des `ICommand` (zones de détail / clic-pour-broder, colonnes de satin à rails, retouche point à
point), non faits. **Aucune famille n'est ✅** : toutes les 🟡 ont un reste explicite dans la matrice. Desktop V2 n'est donc pas
encore le desktop principal.


## 2026-11-04 — Session 1 (suite 15) : clic-pour-broder, zones de détail, retouche de points (branche `feat/desktop-v2-last-families`)

- **Famille 10** : `core/source_image_tools` (clic-pour-broder : `grow_region` + `make_click_to_stitch_command` ; pinceau :
  `PaintDetailMaskCommand` / `ClearDetailMaskCommand`), image source en calque semi-transparent et masque de détail en
  surimpression, barre « Outils d'image » (tolérance, type, rayon). Catch2 `[source]` (6 cas), QTest ×4.
- **Famille 26** : `core/point_edit` (vue des points d'un objet, déplacer / supprimer / rétablir / insérer / tout effacer via
  `EditStitchPointsCommand` et `DiscardOverridesCommand`, avec l'empreinte du moteur : un objet périmé est refusé), outil
  `stitch.points` (nouveau en V2, 21 outils au registre), poignées sur le canevas. Catch2 `[points]` (4 cas), QTest
  `thePointToolMovesDeletesAndDiscardsStitchEdits`.
- État de la matrice : ✅ 2 · 🟡 40 · ⬜ 2 (restent : 9 IA, exception justifiée ; 19 colonnes de satin à rails).
- **Famille 19** : `core/satin_column` (création d'une colonne à deux rails depuis des paires de points, rails croisés signalés,
  déplacement / insertion de nœud de rail), outil `satin.column` (clics par paires, Entrée). Catch2 `[satin]` (4 cas), QTest
  `theSatinColumnToolBuildsRailsFromPairsOfClicks`. Matrice : ✅ 2 · 🟡 41 · ⬜ 1 (seule la famille 9, IA, reste ⬜).

## 2026-11-04 — Session 1 (suite 16) : persistance de l'espace de travail (branche `feat/desktop-v2-persistence`)

- `core/workspace_state` (Qt-free) : liste des fichiers récents (`pushRecent`, `pruneMissingRecent`, `loadRecentList` /
  `saveRecentList`) et `AutosaveStore` (écriture atomique `.tmp` puis renommage, `load`, `clear`). Catch2 `[workspace]`.
- Interface : `ShellWindow::setStateDirectory` (appelé par `main.cpp`, jamais par les tests), sous-menu Fichier > Fichiers
  récents, minuterie d'autosave 60 s, récupération proposée au démarrage, effacée à l'enregistrement et à la sortie
  propre ; Édition > Historique (annule k+1 étapes). QTest `autosaveRecoveryRecentFilesAndHistoryMenuWork`.
- Familles 2, 4 et 34 : restent 🟡 (migration de schéma, rapport de plantage).

## 2026-11-04 — Session 1 (suite 17) : parité avec l'ancien bureau, lot « disposition »

- `core/arrange_batch` : aligner (6 modes sur la boîte de l'ensemble), répartir (espaces égaux, H/V), grouper, dégrouper,
  position / taille / rotation libre numériques, tous par lots `project_ops` (`place`, `group`, `ungroup`). Catch2 `[arrange]`.
- Interface : Édition > Aligner, Répartir, Grouper (Ctrl+G), Dégrouper (Ctrl+Maj+G), Position et taille… ; mêmes entrées
  dans la palette Ctrl+K. QTest `alignGroupAndNumericPlacementWorkFromTheEditMenu`.

## 2026-11-04 — Session 1 (suite 18) : parité avec l'ancien bureau, lots « image », « production » et « aide »

- `core/image_prep` : luminosité / contraste, niveaux de gris, netteté, CLAHE, rotations, retournements, recadrage, chacun une
  `AppendImageOpCommand` annulable (la segmentation est invalidée par le moteur). Menu Image + palette. Catch2 `[imageprep]`.
- `core/production_sheet` : fiche de production (séquence effective : dimensions, comptes, plan de fils par bloc, durée).
  Fichier > Fiche de production (PDF) met en page l'aperçu et le plan de fils (QPdfWriter, tâche annulable). Catch2 `[sheet]`.
- Aide > Raccourcis et commandes (texte généré depuis `commandEntries`) et À propos.
- Constat : l'ancien bureau n'importe pas le DST (seulement la CLI) ; la ligne 3 n'est donc pas un écart de parité.

## 2026-11-04 — Session 1 (suite 19) : parité avec l'ancien bureau, lot « fils, ordre, coloris »

- `core/thread_plan` : plan de fils (objets groupés par fil dans l'ordre de couture), fusion, recoloration, visibilité, déplacement dans l'ordre de couture (refus si l'ordre des calques casse), coloris (ajout, activation, renommage, suppression) ; tout via `ICommand`, une étape d'annulation par action. Catch2 `[threads]`.
- Interface : dock « Fils » à trois onglets (Fils / Ordre / Coloris). QTest `threadPanelListsThreadsOrderAndColorways`.

## 2026-11-05 — Session 1 (suite 21) : carte de densité

- Fusion dans main des PR V2 #253 (protection des documents), #255 (outils lisibles, icônes) et #257 (navigation précise) après résolution de conflits ; #254 et #256 fermées (doublons). Les tests `Ignorer` cliquent désormais le vrai bouton : `QMessageBox::done(Discard)` sans clic renvoie le bouton d'échappement.
- `core/density_service` : `run_density` (séquence effective → `stitch_analysis::compute_density_map` + zones sur-denses) sur un instantané, en tâche annulable ; image RGBA d'une case par pixel, révision analysée. Interface : commande Analyse › Afficher/Masquer la carte de densité, surimpression non cliquable, recalcul automatique après modification. Catch2 `[density]`, QTest `densityMapOverlaysThePreviewAndFollowsTheDocument`.

## 2026-11-05 — Session 1 (suite 22) : finalisation V2, tranche 1 (nœuds du contour)

- Début de la mission « finalisation V2 » sur une branche unique `feat/desktop-v2-finalisation` ; matrice de suivi factuelle dans `09-matrice-finalisation.md`.
- `core/node_edit` : poignées des nœuds du contour de l'objet sélectionné, aperçu pur géométrie d'un glisser (aucune régénération de points), déplacement validé en UNE commande (`TransformNodesCommand`, refus si le contour se croiserait), insertion exacte par double-clic (`InsertNodeCommand`), service de remodelage de segment. Outil « Nœuds du contour » (B) avec poignées carrées, contour d'aperçu (rouge pointillé si refusé), Échap pour annuler. Catch2 `[nodes]` (5 cas), QTest `nodeToolDragsInsertsAndUndoesThroughTheCanvas` et `escapeCancelsANodeDragWithoutChangingTheDocument`.
- Constat d'intégration : un build incrémental raté (erreur de compilation ignorée) avait fait tester un ancien binaire et simuler des « blocages » ; vérifier `grep ' error '` dans le journal de build avant de conclure. #262 (thème) reste à valider : son blocage s'est reproduit après reconstruction propre.

## 2026-11-05 — Session 1 (suite 23) : finalisation V2, tranche 2 (rails de satin)

- L'outil « Nœuds du contour » gère aussi les colonnes satin : poignées des deux rails (rail A/B), glisser un ou plusieurs nœuds d'un même rail (UNE `CompositeCommand` annulable, aperçu des deux rails), double-clic pour insérer un nœud (`InsertSatinRailNodeCommand`), barreaux dessinés en repères discrets, largeur aux barreaux annoncée avec conseil tatami au-delà de `max_width`. Pas de segment de retour sur un rail ouvert. Limite connue : les barreaux ne suivent pas les rails, la largeur affichée est celle des barreaux stockés.
## 2026-11-04 — Session 1 (suite 20) : parcours guidé

- Retour utilisateur : « le parcours n'est pas clair ». Ajout d'un écran d'accueil (ouvrir une image ou un projet, projet vierge, projets récents, explication des quatre étapes) affiché tant qu'aucun projet n'est ouvert.
- `core/workflow_guide` : bandeau Importer › Retoucher › Contrôler › Exporter calculé depuis l'état du document (objets, analyse à jour, erreurs, export à jour) avec un bouton « étape suivante » ; une modification rouvre l'étape de contrôle. Catch2 `[guide]`, QTest `theWelcomePageExplainsTheJourneyAndTheGuideFollowsTheDocument`.

## 2026-11-05 — Session 1 (suite 24) : finalisation V2, tranche 3 (compagnon IA)

- Constat : la PR #252 avait été fusionnée à son premier commit ; l'écran d'accueil et le parcours guidé n'étaient pas dans main. Réintégrés dans cette branche (commit empty de l'installateur ignoré : déjà présent).
- Pont live pour la V2 : `DocumentSession::liveBridge()` lie le pont au projet ET à la pile d'annulation de l'utilisateur (un seul modèle documentaire, un seul historique) ; `core/live_companion` (journal des demandes, passerelle d'approbation, notification de l'interface après chaque modification d'agent) ; `ui/live_server` (socket locale accessible à l'utilisateur seul, jeton aléatoire, fichier de découverte) ; `ui/companion_panel` (dock « Compagnon IA » : état, mode d'emploi, journal appliqué/refusé, annulation) et boîte d'approbation avant/après. Désactivé au démarrage ; la désactivation invalide les calculs d'agent en cours.
- Tests : Catch2 `[companion]` (approbation → une étape annulable avec l'historique de l'utilisateur ; refus → document inchangé ; dry-run sans approbation ; remplacement du document → session périmée ; pont fermé) et QTest `agentChangesTheOpenProjectThroughTheLiveBridgeAfterApproval` (vraie socket : mauvais jeton, refus, accord, annulation, désactivation).

## 2026-11-05 — Session 1 (suite 25) : finalisation V2, tranche 4 (axe du fondu)

- `core/fade_axis` + outil « Axe du fondu » (L) : quatre poignées au canevas (début et fin de la transition, déplacement de l'axe, rotation à 90°), aperçu de l'axe pendant le glisser (géométrie pure, aucune régénération de points), Maj = extrémités symétriques, validation en UNE commande (`make_edit_gradient_fill_command`), résumé « Transition de x % à y % de la forme ». La rotation conserve la zone de transition (fraction de la forme), pas la longueur : comportement du moteur, testé. Réutilise le mode canevas des nœuds. Touche L (Maj+F est déjà « cadrer la sélection »).

## 2026-11-05 — Session 1 (suite 26) : finalisation V2, tranche 5 (optimisation avec preuve)

- `core/path_optimizer` : propose un nouvel ordre (4 stratégies, affinage 2-opt optionnel, départ/arrivée du motif) calculé sur une copie, puis MESURE la séquence réelle (`effective_sequence`) avant et après : changements de fil, sauts, coupes, distance à vide entre objets d'après les extrémités réelles, points. Le test vérifie que la séquence obtenue après acceptation correspond aux mesures annoncées. Un compromis (moins de changements de fil mais plus de déplacements) est signalé comme tel et non présenté comme une amélioration. Acceptation = `ReorderEmbroideryCommand`, un pas d'annulation ; proposition périmée refusée (révision).
- Interface : dock « Optimisation » (stratégie, affinage, mesures avant → après, verdict, Accepter / Refuser), tâche annulable hors fil d'interface, commande `optimize.order`.

## 2026-11-05 — Session 1 (suite 27) : finalisation V2, tranche 6 (déformation du tissu)

- `core/fabric_risk` : solveur mécanique 2D du moteur sur la séquence effective, en tâche annulable ; carte RGBA de l'indice de plissement (1 = seuil critique), flèches de déplacement prévu (≤ 12 × 12, amplifiées ×20 à l'écran et annoncées comme telles), verdict chiffré. Le modèle n'est pas calibré : `calibrated` reste faux et le verdict l'écrit explicitement (test dédié). Commande `analysis.fabric` (afficher / masquer), recalcul automatique après modification.

## 2026-11-05 — Session 1 (suite 28) : finalisation V2, tranche 7 (mesures de performance)

- Banc `[.perf]` (1 / 100 / 500 / 1 000 objets) et rapport `10-performances.md`. Résultats Debug : retour visuel local p95 0,02 ms ; gestes ordinaires (+1 objet, déplacer, copie) ≤ 9,3 ms à 1 000 objets ; séquence effective 270 ms et aperçu réaliste 506 ms à 1 000 objets (en tâche de fond) ; ajout d'un lot de 1 000 objets 276 ms sur le fil d'interface (reste à déplacer en tâche). Le plafond de 50 ms du retour local est contrôlé par un test ; rien d'autre n'est affirmé sans mesure.

## 2026-11-05 — Session 1 (suite 29) : finalisation V2, tranche 8 (import avec segmentation en aperçu)

- Constat : la V2 numérisait toujours avec les réglages par défaut (8 couleurs…), sans aucune interface de réglage. Nouvelle fenêtre d'import : hauteur, nombre de couleurs, plus petite région, lissage, fond, style ; l'image d'origine et la segmentation qui en résultera sont affichées côte à côte (`core/segmentation_preview`, même fonction que la numérisation, tâche annulable, résultat calculé pour d'autres réglages jamais affiché). La validation numérise avec ces réglages. `openImage` accepte désormais des `DigitizeSettings` ; l'option `--image` de la ligne de commande garde le comportement par défaut.

## 2026-11-05 — Session 1 (suite 30) : finalisation V2, tranche 9 (magnétisme)

- `core/snap` : accrochage des bords et du centre de la sélection sur les bords, centres des autres objets et l'axe du motif, avec repères d'alignement ; seuil de 6 pixels de vue converti en mm selon le zoom ; Alt suspend l'accrochage. Le canevas appelle le fournisseur pendant le glisser (retour visuel immédiat, repères roses en pointillé) et la valeur validée est celle qui était affichée (une seule commande de déplacement). Ne touche que le déplacement d'objets pour l'instant.

## 2026-11-05 — Session 1 (suite 31) : finalisation V2, tranche 10 (courbes)

- Commandes « Convertir les contours en courbes » et « Lisser les contours » (une commande annulable pour toute la sélection) ; remodelage direct d'un segment : un appui sur le tracé (hors poignée) saisit le segment de la VRAIE courbe (abscisse sur la cubique, pas sur la corde), aperçu pendant le glisser, refus si le contour se croiserait, nœuds immobiles, un pas d'annulation. Un rectangle n'a que des coins francs (> 60°) : le refus « rien à convertir » est le comportement du moteur et reste affiché tel quel. Les tests utilisent un octogone.

## 2026-11-05 — Session 1 (suite 32) : finalisation V2, tranche 11 (extraction de ShellWindow)

- `ui/node_edit_controller` : l'état et la logique des gestes d'édition directe (poignées, sélection de nœuds, segment saisi, axe de fondu) quittent `ShellWindow` ; le contrôleur ne reçoit qu'un contexte (`Host` : session, canevas, repère, outil actif, cible, message) et passe par les services du cœur. `ui/analysis_overlays` : cartes de densité et de risque du tissu (tâches, révisions, recalcul). Extraction mécanique : aucune ligne de comportement modifiée, les tests existants servent de filet (92 QTest, 179 cas Catch2 verts). `core/` reste indépendant de Qt.

## 2026-11-05 — Session 1 (suite 33) : finalisation V2, tranche 12 (thème Atelier)

- Fusion de la branche du thème « Atelier » (#262) dans la finalisation. Le blocage à 100 % de processeur observé en la testant seule contre l'ancien main ne se reproduit pas sur la branche actuelle : la suite complète (93 QTest) passe en moins de deux minutes. Cause exacte non isolée ; je ne prétends donc pas l'avoir corrigée. #262 devient redondante avec la PR finale.

## 2026-11-05 — Session 1 (suite 34) : finalisation V2, tranche 13 (Plume)

- Outil Plume (`draw.bezier`, P) : `core/pen_shape` construit le contour (nœuds d'angle, nœuds lisses à tangentes symétriques) et crée l'objet vectoriel ET l'objet de broderie en UNE commande (`AddObjectBatchCommand`, mêmes réglages que `add_shape`) ; refus clairs sans trace dans l'historique. Canevas : aperçu élastique avec nœuds et poignées, fermeture par clic sur le premier nœud. Les tests qui prenaient `draw.bezier` comme exemple d'outil non migré utilisent désormais `bucket`.

## 2026-11-05 — Session 1 (suite 35) : finalisation V2, tranche 14 (suppression de nœuds)

- Suppr dans l'outil Nœuds retire les nœuds choisis (jamais l'objet) : simulation sur une copie (3 nœuds minimum pour un contour fermé, aucun croisement introduit), puis UNE commande composite de `RemoveNodeCommand` en ordre décroissant ; annulation exacte de la forme d'origine (testée nœud par nœud). Refusé sur les rails de satin.

## 2026-11-05 — Session 1 (suite 36) : finalisation V2, tranche 15 (Crayon)

- Outil Crayon (`draw.freeform`) : le trait est dédoublonné, refermé, contrôlé (aire, croisements) puis ajusté par `fit_curves` ; la tolérance suit la taille du dessin (2 % de la diagonale, entre 0,3 et 3 mm) et se resserre jusqu'à obtenir au moins quatre nœuds (une courbe fermée à deux nœuds est valide mais difficile à retoucher). Création partagée avec la Plume (`commitShape`, une commande). Premier essai à tolérance fixe (0,3 mm) : 47 nœuds pour 90 points, c'est-à-dire du bruit ajusté ; corrigé.

## 2026-11-05 — Session 1 (suite 37) : finalisation V2, tranche 16 (inspecteur : niveaux et recherche)

- Inspecteur : champ de recherche (sans casse ni accents, sur le libellé et la clé du réglage) et case « Réglages avancés (N) » ; 13 réglages avancés ajoutés à la table `param_spec` avec les clés et plages du registre de champs du moteur, chacun relu (`readParam`) et testé. Les essentiels restent seuls visibles par défaut ; une recherche révèle les avancés qui correspondent. Niveau « expert » non traité.
- Aussi : test d'identité octet pour octet du fichier machine après enregistrement et réouverture d'un projet numérisé et retouché (puis modification suivie d'une annulation).

## 2026-11-05 — Session 1 (suite 38) : finalisation V2, tranche 17 (poignées de tangentes)

- Les nœuds courbes exposent leurs poignées de tangente (positions absolues) ; l'interface n'affiche que celles des nœuds choisis (cercles + tige), pour ne pas noyer le canevas. Glisser une poignée : aperçu géométrique pur, validation en UNE commande composite (`SetNodeHandleCommand` / `MoveSatinRailHandleCommand`), nœud immobile, poignée opposée d'un nœud lisse gardée colinéaire avec sa longueur. Une poignée tirée sur un rail droit en crée une.

## 2026-11-05 — Session 1 (suite 39) : finalisation V2, tranche 18 (courbe du fondu, compagnon, correctif MSVC)

- Courbe de transition du fondu : cinq courbes (mêmes formules que le moteur, testées monotones de 0 à 1), commandes « Courbe du fondu : … », résumé de transition qui nomme la courbe.
- Compagnon : bouton « Copier la commande de connexion (Claude Code) » (`claude mcp add openstitch -- <chemin>/mcp/openstitch-mcp --live`, chemin de l'installateur, repli sur le dossier de l'exécutable).
- **Correctif CI MSVC** : `QPointer<ImportDialog>` sur un type incomplet cassait la cible `openstitch_v2` (main.cpp) alors que les tests compilaient. Leçon : construire AUSSI l'exécutable avant de pousser ; c'est désormais fait à chaque lot.

## 2026-11-05 — Session 1 (suite 40) : retour visuel réel et correctifs d'interface

- **Le retour visuel est possible** : lancer `openstitch_v2 --demo-shapes --screenshot out.png` avec `QT_QPA_PLATFORM=windows` (plateforme native) donne une vraie capture avec polices, thème et fils cousus ; la plateforme `offscreen` des tests, elle, n'a pas de police (d'où les carrés des captures précédentes, que j'avais acceptés sans chercher). Première capture réelle : libellés du rail tronqués (« Recadr…tangle »), largeurs irrégulières, onglets des panneaux de droite écrasés (« C… »). Corrigés : noms courts dans le rail (nom complet en infobulle et dans la barre d'état), largeur régulière, onglets jamais tronqués (défilement). Captures avant/après dans `captures/v2-avant-rail-onglets.png` et `v2-apres-rail-onglets.png`.

## 2026-11-05 — Session 1 (suite 41) : correction de la segmentation après numérisation

- Retour utilisateur : « plus la main sur la segmentation (supprimer / fusionner des régions) ». Nouvel outil **Régions** (X, menu Outils › Corriger la segmentation) : carte des régions à plat sur le motif, clic pour choisir, panneau dédié (liste, Supprimer, Fusionner dans, Nettoyer les îlots, Régénérer).
- Cœur `region_edit` (sans Qt, 5 cas Catch2) ; contrôleur `region_edit_controller` ; chaque correction = une commande du moteur ; la régénération refait les objets depuis la segmentation corrigée en tâche, refusée si la révision a changé, **une seule étape d'annulation**.
- Limite : la régénération remplace les objets (retouches manuelles perdues, annulable) ; recoloration de région, fusion de couleurs et frontières non faits.

## 2026-11-06 — Session 2 : lot 1, barreaux de satin

- Cœur `satin_column` : ajouter / insérer entre deux barreaux / déplacer / retirer un barreau (une commande annulable chacun, refus sans trace : extrémités confondues, hors limites, moins de deux barreaux), `widestRungGap` et `redundantRung` (choix déterministe), `diagnoseSatin` (croisements en entiers 64 bits, aucun flottant pour décider). IHM : commandes « Satin : … » dans la palette. Pas de glisser au canevas pour l'instant. Validation : syntaxe seule en local (pas de Qt ni vcpkg dans ce conteneur), tests lancés par la CI.

## 2026-11-06 — Session 2 : lot 2, comparaison du tirage et étalonnage

- `core/pull_calibration` : `PullCalibration` (pièce-test, tableau nominal / prédit / mesuré, CSV, saisie une à une, ajustement en grille, DST de la pièce) et `compareProjectPull` (séquence effective contre séquence simulée : déplacement moyen et maximal, emprise). Boîte `CalibrationDialog` (sans logique métier) et commandes `analysis.pull` / `analysis.calibration`. Choix assumé : l'ajustement n'est qu'une proposition ; le modèle n'est jamais déclaré calibré, et les coefficients ne sont pas écrits dans le projet.

## 2026-11-06 — Session 2 : lot 3, options machine d'export

- `core/export_settings` : lecture en mm des finitions du moteur, validation (bornes, point max > point min), UNE commande par groupe (machine, cadre, finitions) ; comparaison en µm pour ne laisser aucune trace quand rien ne change. Bibliothèque de 16 cadres et 6 profils de machine du moteur, `checkExport` sur la séquence effective (points hors zone brodable, champ de la machine, format accepté). Fenêtre `ExportOptionsDialog` sans logique métier ; commandes `export.options` et `export.check`.

## 2026-11-06 — Session 2 : lot 4, fils, texte et préférences

- **Nuanciers** (`core/thread_palette_service`, `ThreadPaletteDialog`) : recherche, plus proches, assignation, mélange de deux fils. Le mélange est simulé en lumière linéaire sur les 12 fils les plus proches, par pas de 10 % ; il dit quand un fil seul fait aussi bien.
- **Texte** : `text.add` reçoit les réglages typographiques du moteur ; `text.edit` (hors catalogue pour ne pas casser l'invariant « chaque action se construit avec des valeurs par défaut ») retrouve le texte propriétaire des lettres sélectionnées (`text_owner`) et ouvre le dialogue prérempli.
- **Préférences** (`core/preferences`) : thème et densité. Constat : le thème choisi n'était jamais enregistré. Densité = mise à l'échelle des seules lignes `padding` de la feuille de style. Langue volontairement absente.

## 2026-11-06 — Session 2 : lot 5, chat intégré

- Constat : la V1 avait déjà tout le côté agent (`ClaudeCompanionBackend`, `LiveToolHost`, réglages, stockage de clé) mais dans `apps/desktop`, inutilisable depuis la V2 sans dépendre de l'autre application (et leurs en-têtes homonymes — `calque_panel.hpp`, `live_server.hpp`… — rendent un `include_directories` partagé dangereux). Choix : copie de 5 fichiers dans `apps/desktop_v2/ui` sous l'espace de noms `desktop_v2` (mêmes clés de réglage : la clé et l'activation déjà saisies en V1 sont reprises). Dette assumée : deux copies à faire converger quand la V1 sera retirée.
- `ChatPanel` ne fait que des widgets ; le document n'est jamais modifié par le panneau : les outils de l'agent passent par `LiveCompanion` (approbation avant/après). `ensureCompanion()` crée le pont sans serveur local, le chat n'en a pas besoin.
- Non fait : essai réel avec une clé, fournisseur « processus Claude Code », analyse sémantique automatique.

## 2026-11-06 — Session 2 : lot 6, pinceau de frontière

- `paintRegionStroke` (cœur) : disques de rayon réglable le long du trait, pixels repeints et leurs anciennes étiquettes mémorisés (annulation exacte, comptes de pixels recalculés des deux côtés, ordre des étiquettes déterministe). Refuse d'effacer entièrement une région voisine (une région vide casserait la carte ; « Supprimer » existe pour cela). Outil `region.brush` (U) sur le geste « trait » du canevas ; la région se choisit avec l'outil Régions (X).
- Choix : le pinceau ne touche que la carte d'étiquettes, jamais les objets : la régénération reste l'étape explicite (et annulable) déjà en place.
- Non fait : segmentation IA (SAM), toujours hors priorité produit ; le moteur et la V1 la portent, la V2 n'affiche pas encore son état de disponibilité.

## 2026-11-06 — Session 2 : « Mes cadres »

- `core/hoop_store` : liste de cadres personnels (nom, forme, dimensions, rayon, marge) dans `hoops.txt` du dossier d'état, à côté des préférences ; un cadre = une ligne `nom;forme;l;h;rayon;marge`. Lecture tolérante (une ligne corrompue n'en cache pas une autre), écriture atomique, nom unique sans tenir compte de la casse, 50 cadres au plus, rien n'est promis si l'écriture échoue (la liste en mémoire revient à l'état précédent).
- Choix : un cadre personnel porte SA marge, celle enregistrée ; le champ « marge » de la fenêtre ne s'applique qu'aux cadres de la bibliothèque. Les cadres personnels vivent hors document : ouvrir un projet sur une autre machine n'en a pas besoin, le cadre est déjà écrit dans le projet.

## 2026-11-06 — Session 2 : fin de la duplication du compagnon V1 / V2

- Les 5 fichiers du compagnon (fournisseur Claude, frontière `CompanionBackend`, réglages, boîte de dialogue) quittent `apps/desktop` et la copie de la V2 : ils vivent dans `apps/companion` (bibliothèque statique `openstitch_companion_backends`), liée par les deux bureaux. Déplacement par `git mv` : l'historique suit. Même espace de noms qu'avant (`openstitch::desktop`) pour ne pas toucher la vingtaine de fichiers de la V1 ; la V2 le ramène sous `desktop_v2` par des `using` dans un seul fichier (`ui/companion_shared.hpp`).
- **Effet de bord utile** : le garde `check_companion_security` ne regardait que `apps/desktop/…` ; le chat, le pont et la copie V2 y échappaient. Il couvre maintenant `apps/companion`, le pont et le chat de la V2 (42 fichiers examinés, aucune violation).
- Message d'erreur de clé rendu neutre (« réglages du compagnon IA »), car il citait un menu de la V1 qui n'existe pas en V2.
- **À savoir** : `update_translations` (lupdate) ne scanne que les sources de `openstitch_desktop_widgets` ; les chaînes de la nouvelle bibliothèque n'y sont plus. Les traductions déjà présentes restent valides à l'exécution, mais la prochaine mise à jour des `.ts` les marquerait obsolètes tant que la bibliothèque n'est pas ajoutée aux sources de lupdate (option `SOURCE_TARGETS` de `qt_add_translations`, non essayée ici faute de Qt).

## 2026-11-06 — Session 2 : sélection après « Ajouter du texte »

- Trouvé par la CI Windows (QTest `textEditCommandNeedsAWholeTextAndRewritesItInOneStep`) : l'opération `add_text` rend l'identifiant de l'objet TEXTE, que la fenêtre sélectionnait comme s'il s'agissait d'un objet de broderie. Résultat réel, pas seulement un défaut de test : après avoir ajouté un texte, « Modifier le texte » restait grisé tant qu'on ne cliquait pas une lettre. La fenêtre sélectionne maintenant les lettres du texte créé ou modifié.

## 2026-11-06 — Session 2 : édition d'une lettre

- Les lettres n'ont pas d'identifiant propre : le rang d'une lettre (espaces exclus, ordre de lecture) est déduit de l'ordre des formes du texte, une lettre pouvant en produire plusieurs (« Lettre A », « Lettre A (2) »). **Garde-fou** : le nombre de lettres trouvées doit égaler celui du texte ; sinon (ligatures, accents combinants, écritures de droite à gauche, caractères invisibles, formes ajoutées ou retirées à la main) la commande refuse avec la raison au lieu de risquer de modifier la mauvaise lettre.
- `set_text` remplace TOUTE la liste de retouches : on renvoie donc la liste complète avec la lettre changée (ou retirée si les réglages redeviennent neutres) ; les autres lettres gardent leurs retouches (testé).
- Limite : l'édition régénère les lettres (nouveaux identifiants), la sélection redevient « toutes les lettres du texte » ; le glisser d'une lettre au canevas n'est pas fait.

## 2026-11-06 — Session 2 : test du chat bloqué par une boîte modale

- Trouvé par le build Windows : `chatPanelTalksToTheProvider…` appelait `newProject()` une seconde fois sur un document « modifié » ; la fenêtre ouvrait sa boîte « Modifications non enregistrées », que rien ne fermait, d'où 300 s puis un arrêt forcé qui empêchait aussi les autres QTest de la V2 de tourner. Défaut du test, pas du chat. Leçon : un test QTest qui remplace le document passe par `session().replaceProject(...)`, jamais par une commande qui peut demander confirmation.
