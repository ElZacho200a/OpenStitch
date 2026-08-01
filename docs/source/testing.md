# Tests

Public : développeur, mainteneur.

## Structure

- **Framework (cœur, cli)** : Catch2 v3 (via vcpkg), intégré à CTest.
- **Framework (UI desktop)** : Qt6::Test (QTest/QSignalSpy), intégré à CTest,
  toujours exécuté **headless** (`QT_QPA_PLATFORM=offscreen`) — voir
  [Tests UI (Qt, desktop)](#tests-ui-qt-desktop).
- **Tests unitaires** : `tests/unit/<lib>/test_*.cpp` (un exécutable par lib).
- **Test d'intégration** : `tests/integration/test_pipeline.cpp` (chaîne complète).
- **Golden (SVG de diagnostic)** : `tests/golden/stitch-generation/`.
- **Garde structurelle CI** : `tests/check_no_raw_sequence_bypass.cmake`
  (invoquée via `add_test`, cf. Lot 8.1) — échoue si un site de production
  hors `libs/stitch_generation/` appelle `generate_sequence()` directement
  sans annotation `raw-sequence-ok:`, contournant les retouches manuelles.
- Total au dernier passage vérifié : **340 tests CTest**, 100 % réussis.

## Encadré de traçabilité (dernier passage vérifié)

Un simple « 209/209 » devient vite périmé ; voici le contexte exact du dernier
passage vérifié manuellement. Régénérez ces valeurs avant toute publication.

| Élément | Valeur |
|---|---|
| Commit (état du code testé) | `df48ca6` — ajout atomique de guides internes par jonction |
| Compilateur | MSVC toolset 14.50 (Visual Studio 2026) |
| CMake | 4.4.0-rc3 |
| Configurations | Debug **et** Release |
| Résultat CTest | 340 / 340 réussis |
| Tests désactivés | 0 |
| Fichiers de tests d'intégration | 1 (`tests/integration/test_pipeline.cpp`) |
| Suites Qt (UI desktop) | 5 exécutables CTest (`tests/unit/desktop/`), 37 fonctions de test QTest |
| Tests sur machine réelle | 0 |
| Couverture de code | non mesurée |

Note : chaque `TEST_CASE` Catch2 (ou fonction de test QTest) est enregistré
comme un test CTest (via `catch_discover_tests` côté Catch2, `add_test` par
suite côté QTest) ; le nombre d'**assertions** est supérieur. Le chiffre 340
compte les cas de test (dont la garde structurelle CI), pas les assertions.

## Exécution

```powershell
ctest --preset msvc-debug                 # tous les tests
ctest --preset msvc-debug -R tatami       # filtre par nom
build\msvc\tests\unit\stitch\Debug\test_stitch.exe "[nom]"   # un exécutable
```

## Correspondance modules ↔ tests

| Module | Tests |
|---|---|
| core | `tests/unit/core/test_units.cpp`, `test_ids.cpp` |
| geometry | `test_simplify.cpp`, `test_clean.cpp`, `test_offset.cpp`, `test_polyline.cpp` |
| image | `test_image_load.cpp`, `test_ops.cpp` |
| segmentation | `test_segmentation.cpp` |
| vectorization | `test_vectorize.cpp` |
| stitch / stitch_generation | `test_stats.cpp`, `test_running_stitch.cpp`, `test_generate.cpp`, `test_tatami.cpp`, `test_satin.cpp`, `test_satin_pairing_metrics.cpp`, `test_routing.cpp`, `test_overrides.cpp` |
| autodigitize | `test_autodigitize.cpp` |
| auto_satin | `tests/unit/auto_satin/test_pipeline.cpp`, `test_columns.cpp` |
| stitch_analysis | `test_analyze.cpp` |
| optimization | `test_order.cpp` |
| commands | `test_undo_stack.cpp` |
| formats | `test_dst.cpp`, `test_svg.cpp` |
| project_io | `test_roundtrip.cpp`, `test_overrides_persistence.cpp` |
| desktop (UI, Qt) | `tests/unit/desktop/test_canvas_view.cpp`, `test_node_handle.cpp`, `test_document_panel.cpp`, `test_properties_panel.cpp`, `test_main_window.cpp` |
| intégration | `tests/integration/test_pipeline.cpp` |

## Tests UI (Qt, desktop)

Fondation ajoutée pour vérifier réellement l'interface (et pas seulement le
cœur) : la sélection au canevas, le déplacement d'un nœud, la synchronisation
canevas/liste/inspecteur, sans jamais comparer de pixels ni dépendre d'un
écran.

### Architecture de testabilité

`apps/desktop/CMakeLists.txt` sépare les sources en une bibliothèque statique
`openstitch_desktop_widgets` (tout sauf `main.cpp`), liée à la fois par
l'exécutable `openstitch` et par les tests. Pour rendre `MainWindow`
elle-même instanciable en test (deuxième tranche, voir plus bas), trois seams
minimaux s'y ajoutent, sans changer son comportement en production :
`QSettings()` par défaut (au lieu d'une organisation/application codées en
dur) lit l'organisation/application déjà posées sur `QCoreApplication` dans
`main.cpp` — un test peut donc les rediriger vers un fichier `.ini` temporaire
avant de construire `MainWindow`, sans jamais toucher au registre Windows
réel ; `MainWindow::applyLoadedProject(Project)` (**privée**, appelée depuis
`loadProject()` et réutilisée telle quelle) applique un projet déjà construit
sans passer par `QFileDialog` — accessible aux tests via `friend class
MainWindowTest` (déclaré dans `main_window.hpp`, `MainWindowTest` vit dans
`openstitch::desktop`), pas via une méthode publique ajoutée uniquement pour
les tests (revue corrective, voir plus bas) ; `QAction::setObjectName(...)`
sur une poignée d'actions (`action_undo`, `action_redo`,
`action_deleteRegion`, `action_createStitch`) pour les retrouver par
`findChild` sans dépendre du texte traduit. Aucun membre n'est rendu public
au-delà de l'API de production existante.
Chaque suite est un exécutable QTest (`QTEST_MAIN`), exécuté par
CTest avec `QT_QPA_PLATFORM=offscreen` (propriété `ENVIRONMENT`) et le
dossier `bin` de Qt ajouté au `PATH` (propriété `ENVIRONMENT_MODIFICATION` —
Qt n'est pas déployé à côté de chaque exécutable de test comme il l'est pour
`openstitch` via `windeployqt`). Les objets métier non enregistrés comme
type Qt (`ObjectId`, `RegionId`, `StitchParams`) sont observés par connexion
directe (lambda) plutôt que par `QSignalSpy`, pour ne pas avoir à leur
ajouter `Q_DECLARE_METATYPE` (le cœur ne dépend jamais de Qt) ; `QSignalSpy`
reste utilisé pour les signaux à types Qt natifs (`QPointF`, `QRectF`).

### Matrice couverte

| Composant | Scénario testé | Résultat métier vérifié |
|---|---|---|
| `CanvasView` | zoom avant/arrière, `fitCanvas` | échelle (`pixelsPerMm`) change dans le bon sens, `viewChanged` émis |
| `CanvasView` | clic hors mode recadrage | `canvasClickedMm` émis avec la position scène exacte |
| `CanvasView` | mode recadrage actif | clic simple **n'émet plus** `canvasClickedMm` |
| `CanvasView` | glisser-rectangle en mode recadrage | `cropSelectedMm` émis avec le rectangle réellement dessiné (régression, voir plus bas) |
| `NodeHandleItem` | glisser une poignée | callbacks `onMoved`/`onReleased` reçoivent la position scène exacte, l'item a bougé |
| `NodeHandleItem` | clic sans glisser | `onReleased` appelé une fois, position inchangée |
| `DocumentPanel` | `refresh(project)` | listes Objets/Régions peuplées, id stocké (`Qt::UserRole`) correct |
| `DocumentPanel` | sélectionner une ligne Objets | `embroiderySelected` émis avec le bon `ObjectId` |
| `DocumentPanel` | `syncSelection` (retour canevas -> panneau) | **ne réémet aucun signal** (évite la boucle de sélection) |
| `PropertiesPanel` | `showEmbroidery` | formulaire peuplé aux bonnes valeurs, **aucun** `paramsEdited` pendant la construction |
| `PropertiesPanel` | éditer un champ du formulaire | `paramsEdited` émis avec le champ modifié à jour, les autres champs préservés |
| `PropertiesPanel` | changer de sélection (`showInfo` après `showEmbroidery`) | l'ancien formulaire est bien détruit (pas de contrôles fantômes) |
| `MainWindow` | clic canevas sur un objet vectoriel | `DocumentPanel` (liste Objets) et `PropertiesPanel` (spin boxes) se synchronisent sur le point de contour rattaché ; `action_createStitch` s'active |
| `MainWindow` | sélection région (liste) puis sélection objet (canevas) | `action_deleteRegion`/`action_createStitch` s'activent et se désactivent en opposition, selon la priorité de `resolveSelectedEmbroidery`/`onCanvasClicked` |
| `MainWindow` | suppression d'une région (`action_deleteRegion`, sans dialogue) puis undo/redo (`action_undo`/`action_redo`) | la liste Régions de `DocumentPanel` reflète la suppression **et** sa restauration (pas seulement `undoStack_`/le modèle) ; les actions annuler/rétablir s'activent en conséquence |
| `MainWindow` | sélection d'une broderie dans un projet, puis chargement d'un second projet (`applyLoadedProject`) réutilisant le même `ObjectId` de broderie (régression, voir plus bas) | ni `DocumentPanel` (sélection liste Objets) ni `PropertiesPanel` (formulaire) ne montrent la broderie du projet précédent tant qu'aucune sélection explicite n'a été faite dans le nouveau |

`MoveNodeCommand` (la commande undo/redo réellement appliquée quand une
poignée est relâchée) est déjà couverte côté cœur, sans Qt, par
`tests/unit/commands/test_undo_stack.cpp` (cas « AddVectorObject et
MoveNode ») — non dupliqué ici.

### Revue corrective de la deuxième tranche (2026-08-01)

Deux défauts trouvés indépendamment sur les commits `d49e660`/`37ffc27`,
corrigés sans toucher au reste du périmètre de la deuxième tranche :

1. **Seam de test dans l'API publique de production.**
   `MainWindow::loadProjectForTests(Project)` avait été ajouté **public**
   uniquement pour les tests, ce qui contredit la règle du projet (aucune
   API de production élargie pour le seul bénéfice des tests). Corrigé :
   `applyLoadedProject` (déjà privée) reste privée ; `MainWindowTest` est
   forward-déclarée dans `main_window.hpp` et déclarée `friend` de
   `MainWindow`, et vit dans `openstitch::desktop` (comme `MainWindow`
   elle-même) pour que le friend s'applique. Aucune donnée interne
   supplémentaire n'est exposée ; `loadProjectForTests` a été supprimée.
2. **Fuite de sélection de broderie entre deux projets.** `applyLoadedProject`
   réinitialisait `selectedRegion_` et `selectedObject_` mais oubliait
   `selectedEmbroidery_` — défaut déjà présent avant la deuxième tranche (queue
   de l'ancien `loadProject()`, seulement déplacée telle quelle dans
   `applyLoadedProject`). Si le nouveau projet réutilise le même `ObjectId`
   qu'une broderie sélectionnée dans l'ancien (cas réaliste : les projets de
   test allouent des ID déterministes à partir d'un compteur remis à zéro),
   `resolveSelectedEmbroidery()` la retrouvait silencieusement dans le
   nouveau document — l'inspecteur, la sélection dans `DocumentPanel` et la
   barre contextuelle affichaient alors une broderie jamais choisie
   explicitement par l'utilisateur. Corrigé par un `selectedEmbroidery_.reset()`
   ajouté à côté des deux autres réinitialisations. Test de régression :
   `embroiderySelectionDoesNotLeakAcrossProjectLoadWithReusedId` — vérifié en
   retirant temporairement la ligne corrigée : le test échoue bien sans le
   correctif (`objectsList(*docPanel)->currentRow()` reste à `0` et le
   formulaire de l'inspecteur reste peuplé au lieu de revenir à « Aucune
   sélection »).

Audit ciblé du reste de l'état de session réinitialisé par
`applyLoadedProject` : `undoStack_.clear()`, `sequence_.reset()` et
`sequenceImported_ = false` étaient déjà corrects. Les filtres d'affichage
temporaires (`hiddenColors_`, `showType_`, `minAreaMm2_`) et `contextSig_`
persistent délibérément d'un chargement à l'autre (préférences d'affichage de
session, pas des données de document) ; aucun défaut démontré à leur sujet,
donc non modifiés.

### Bug découvert et corrigé par ce lot

Le test du glisser-rectangle a révélé que `CanvasView` utilisait
`fromScenePoint`/`toScenePoint` du signal
`QGraphicsView::rubberBandChanged`, qui accusent un retard d'une étape sur
`viewportRect` (constaté en forçant un glisser à seulement deux évènements de
déplacement espacés) : le rectangle final capturait la position du glisser
**précédent**, pas la position réelle au relâchement. Invisible en usage
réel (le pointeur génère des évènements quasi continus, l'écart est
sub-pixel), mais faux pour un glisser rapide à peu d'évènements
(trackpad, remote desktop). Correctif : reconvertir `viewportRect`
lui-même (`mapToScene(viewportRect).boundingRect()`), toujours à jour.
Test de régression : `cropModeRubberBandEmitsCropSelectedMm`.

### Obstacles de testabilité identifiés

- **`MainWindow` reste un god-object** : constructeur de ~250 lignes
  construisant menus/docks/barres d'outils/panneaux en bloc, logique de
  sélection/filtre/simulation imbriquée dans des lambdas privées
  (`objectPassesFilter`, `embroideryCentroid`…). La deuxième tranche (voir
  ci-dessous) n'a **pas** décomposé la classe — elle a percé trois seams
  minimaux (QSettings injectable, `applyLoadedProject` accessible par
  `friend class MainWindowTest`, `objectName` sur quelques actions) qui
  suffisent à instancier `MainWindow` et à observer son état sans dialogue
  modal. Toute action qui ouvre une boîte de dialogue
  (`QFileDialog`, `QMessageBox`, `QInputDialog`, `QColorDialog`, `QMenu::exec`
  — `openImage`, `saveProject`, `segmentImage`, `onCanvasContextMenu`…) reste
  hors de portée d'un test QTest offscreen automatisé.
- **Sélection rectangulaire d'objets métier absente** : le `RubberBandDrag`
  actuel de `CanvasView` ne sert **que** le mode recadrage
  (`cropSelectedMm` → recadre l'image). Il n'existe aucun mécanisme qui
  sélectionne les objets vectoriels/de broderie contenus dans un rectangle
  glissé — fonctionnalité demandée par l'utilisateur mais pas encore codée
  (hors périmètre de la deuxième tranche, sur consigne explicite). Rien n'a
  donc pu être testé « avec succès » sur ce point ; c'est une fonctionnalité
  **manquante**, pas un test manquant.
- **Déplacement global d'un objet absent** : seul le déplacement d'un nœud
  individuel existe (`NodeHandleItem` → `MoveNodeCommand`, testé). Il n'y a
  pas de commande ni d'interaction pour translater un objet entier (tous ses
  nœuds à la fois) ; pas de `MoveObjectCommand` dans `libs/commands` (hors
  périmètre de la deuxième tranche, sur consigne explicite).

### Deuxième tranche (`MainWindow` — actions/menus, synchronisation, undo/redo)

Les trois points de la roadmap ci-dessous marqués **fait** ont été couverts
sans instancier `MainWindow` via un scénario nécessitant un dialogue modal :
mutation via `action_deleteRegion` (`RemoveRegionCommand`, aucune boîte de
dialogue) sur un document minimal construit directement en mémoire
(`applyLoadedProject`, pas de fichier `.osp` ni d'image chargée depuis le
disque).

### Roadmap (scénarios non automatisables aujourd'hui)

But visé par l'utilisateur, dans l'ordre de valeur/risque :

1. **Sélection rectangulaire d'objets** (prérequis produit avant tout test,
   toujours non codé) : ajouter un mode canevas dédié (distinct du
   recadrage) qui émet les `ObjectId`/`RegionId` sous le rectangle ; puis
   test QTest reprenant le même schéma que `cropModeRubberBandEmitsCropSelectedMm`.
2. **Déplacement global d'un objet** (toujours non codé) :
   `MoveObjectCommand` (cœur, Catch2, translation de tous les nœuds +
   undo/redo — même schéma que `MoveNodeCommand`), puis interaction canevas
   (glisser l'intérieur d'un objet sélectionné) testée en QTest comme
   `NodeHandleItem`.
3. **fait** — **Synchronisation sélection canevas ↔ liste ↔ inspecteur** :
   clic canevas (`CanvasView::canvasClickedMm` appelé directement, sans
   simulation souris pixel-exacte — déjà couverte par `test_canvas_view.cpp`)
   → `MainWindow::onCanvasClicked` → `resolveSelectedEmbroidery` (logique de
   priorité broderie > vecteur, extraite de la triple duplication
   `updateContextToolbar`/`updateInspector`/`syncDocumentSelection`) →
   `DocumentPanel::syncSelection` + `PropertiesPanel::showEmbroidery`.
4. **fait** — **Undo/redo restaure modèle et UI** : le modèle était déjà
   couvert (Catch2, `test_undo_stack.cpp`) ; on vérifie maintenant que la
   liste `DocumentPanel` (pas seulement `project_`/`undoStack_`) se
   **rafraîchit** après un undo puis un redo déclenchés depuis
   `action_undo`/`action_redo`.
5. **fait** — **Menus/actions selon l'état** (`updateActions`) :
   `action_createStitch` (dépend de la sélection d'un objet vectoriel) et
   `action_deleteRegion`/région (dépend de la sélection d'une région **et**
   d'une segmentation présente) togglent correctement, y compris quand une
   sélection en chasse une autre (priorité canevas > liste, cf.
   `onCanvasClicked`).

## Types de vérifications notables

- **DST aller-retour** sur tous les deltas de −121 à +121, déterminisme octet à
  octet.
- **Tatami** : invariant « aucune couture ne traverse le trou » d'un anneau, peu
  de déplacements ; et « aucune couture hors région » sur une forme concave en
  **L** échantillonnée à cinq angles (filet anti-débordement).
- **Auto-numérisation** : une bande fine devient un **tatami** par défaut (satin
  naïf désactivé), et un satin uniquement si `use_naive_satin` est activé.
- **Auto-satin géométrique** (`build_satin_columns`) : formes simples produisent
  une colonne, milieu des barreaux **intérieur à la région**, Y/T → plusieurs
  sections portant un identifiant de jonction commun, anneau fin → quatre
  sections formant un cycle, cercle plein/forme large refusés, déterminisme des
  rails et de la topologie ; SVG dans `tests/golden/auto-satin/`.
- **Persistance des réseaux satin** : aller-retour exact de l'index/nombre de
  sections et des jonctions, ancien satin sans `topology` encore lisible, index
  hors réseau refusé proprement.
- **Appariement rail gauche/rail droit (audit rails, 2026-08-01)** :
  fixtures géométriques dédiées (ruban droit, S, coude 90°, largeur variable,
  cas combiné) mesurant croisements, monotonie sur les deux rails, angle
  fil/normale locale, continuité angulaire, régularité de densité,
  déterminisme — avec comparaison chiffrée à l'ancien algorithme (réplique
  isolée, test uniquement). Voir `docs/source/satin.md` § *Correction de
  l'appariement*.
- **Satin par barreaux** (`fill_satin_columns`) : espacement **médian régulier**
  (colonne droite), barreaux **traversés exactement**, rails de longueurs
  différentes, repli sur `fill_satin` si < 2 barreaux, déterminisme.
- **Édition des guides satin** : QTest headless sur le clic de sélection, l'ajout
  au plus grand intervalle, la suppression avec plancher de deux guides, le
  glisser d'une extrémité projetée sur son rail, le verrouillage visible d'un
  guide terminal de jonction et les parcours undo/redo ; tests cœur sur
  l'espacement minimal, les ordres de guides croissant/décroissant et la
  détection géométrique d'une jonction malgré des barreaux inversés. La collecte
  multi-section est testée sur l'isolation par objet source, l'ordre déterministe
  et le refus d'un réseau incomplet ; la commande coordonnée vérifie undo/redo et
  l'absence de mutation partielle avec cible obsolète ou dupliquée. Un parcours
  QTest clique réellement le guide structurel, ajoute un guide interne aux deux
  branches, puis vérifie l'undo/redo unique sans temporisation ni comparaison de
  pixels.
- **Satin — finitions (Lot 3)** : points courts (inset modifie le rail intérieur,
  remove réduit les pénétrations), split (staggered ≠ ligne centrale, jitter
  déterministe), terminaisons (taper réduit la largeur au bout sans l'annuler) ;
  aller-retour `.osp` des modes.
- **Satin — sous-couches + compensation (Lot 4)** : center/edge/zigzag = 4 passes
  distinctes ordonnées ; pull élargit **un seul côté** (asymétrique) ; push étend
  le bout ; le générateur tague les sous-couches `Underlay` et le satin
  `TopStitch` ; aller-retour `.osp`.
- **Satin — entrée/sortie + lock (Lot 5)** : `lock_stitches` (`None` → vide, les
  autres progressent dans l'axe de couture et restent bornés) ; les locks sont
  émis en passe `Lock` en **exactement deux groupes** (début/fin) ; le point
  d'entrée fait démarrer la couture à la bonne extrémité ; aller-retour `.osp` des
  champs Lot 5 (lock, entrée/sortie).
- **Satin — routage multi-colonnes (Lot 6)** : `route_columns` (liste vide → plan
  vide, une colonne → départ sans saut, réordonnancement minimisant le
  déplacement, orientation par l'extrémité proche, liaison courte → trajet caché /
  longue → saut) ; une jonction commune admissible prime sur une extrémité plus
  proche sans relation topologique, tandis qu'une jonction trop distante est
  ignorée ; le test de génération vérifie que `SatinParams.topology` atteint le
  plan. Un groupe adjacent n'émet qu'un saut initial (le reste cousu en passe
  `Travel`), un groupe éloigné conserve ses sauts.
- **Tatami avancé (Lot 7)** : sous-couche de contour rentrée dans la forme ;
  sous-couche parallèle espacée ; underpath caché convertit au moins un saut en
  trajet cousu **sans jamais** traverser le trou (invariant préservé) et ne
  l'augmente jamais ; déterminisme ; le point d'entrée oriente le démarrage ; le
  générateur tague la sous-couche `Underlay` et le remplissage `TopStitch` ;
  aller-retour `.osp` des champs Lot 7.
- **Retouches manuelles — cœur pur (Lot 8.0, corrigé)** : `fingerprint`
  (FNV-1a 64 bits) stable pour une vue brute identique, sensible à la
  position, au type de commande, à la passe et à l'ordre pris séparément ;
  `raw_slice` reconstruit correctement un objet sur une séquence contiguë et
  sur une séquence entrelacée (synthétique, et via un routage satin réel à
  trajet caché) ; `apply_manual_overrides` valide chaque champ d'un override
  indépendamment sur les entrées `TopStitch` : `moved_to` exige une cible
  `Stitch`, `forced_type` et `trim_after` acceptent une cible `Stitch` **ou**
  `Jump` (Stitch↔Jump bidirectionnel — correctif : `is_overridable_entry`
  n'acceptait auparavant qu'une cible `Stitch`, rendant Jump→Stitch impossible
  malgré le cadrage et le rapport initial de Lot 8.0 ; aucun test ne couvrait
  cette direction), un champ invalide pour sa cible est ignoré sans rejeter
  les autres champs du même override ; laisse un objet `Dirty` strictement
  inchangé (par empreinte **et**, indépendamment, par compteur de points),
  isole chaque objet retouché des autres, résout déterministement les
  doublons d'overrides (dernière entrée du vecteur, en bloc), les overrides
  vides et les index hors bornes ; déterminisme du résultat (séquence
  patchée et liste d'objets `Dirty` triée) vérifié sur deux exécutions
  indépendantes. Aucune commande undo/redo, aucune persistance `.osp`,
  aucune UI dans ce sous-lot (cf. `docs/lot8-manual-editing-design.md`).
- **Retouches manuelles — commandes, persistance, point d'entrée unique
  (Lot 8.1)** : les 4 commandes (`MoveStitchPointCommand`,
  `SetStitchPointTypeCommand`, `SetStitchTrimCommand`,
  `DiscardOverridesCommand`) valident `ObjectId`/`base_index`/état avant toute
  mutation, refusent un objet déjà `Dirty` sans y toucher, restaurent
  exactement l'entrée précédente au `revert` (y compris la transition
  `Clean → ManuallyEdited` et son annulation), et partagent une seule entrée
  d'`overrides` quand deux commandes ciblent le même `base_index` (chaque
  `revert` ne restaure que son propre champ). `SetStitchTrimCommand(false)`
  sur un index sans override existant est un no-op exact (aucune entrée vide
  créée, aucune transition `Clean → ManuallyEdited` fantôme) ; si elle efface
  le dernier champ effectif d'une entrée préexistante (`trim_after` était son
  seul champ posé), l'entrée est retirée plutôt que laissée vide, et `revert`
  la réinsère exactement (`end_stitch_edit` gère ce cas). Persistance `.osp`
  v3 : `overrides`/`editedFingerprint`/`editedPointCount` survivent à un
  aller-retour exact, y compris un `editedFingerprint` **au-delà de 2⁵³**
  (aucune perte de bits via `double` — nlohmann conserve un entier positif en
  `number_unsigned`) ; un fichier v1/v2 sans ces champs charge un objet
  `Clean` ; un index négatif/non entier ou au-delà de
  `numeric_limits<size_t>::max()`, une coordonnée au-delà d'un `int32`, un
  compteur au-delà d'un `uint32`, un type de point inconnu ou une entrée sans
  `index` renvoient une erreur `InvalidFile`, jamais une troncature
  silencieuse ou un plantage. **Revue corrective (2026-08-01)** : `overrides`
  doit être un tableau JSON (objet/scalaire refusé) ; un tableau non vide
  exige la présence **explicite** d'`editedFingerprint`/`editedPointCount`
  (plus de valeur zéro implicite sur un document malformé) ; un tableau vide
  accompagné de ces mêmes clés est normalisé en `Clean` sans erreur
  (métadonnées orphelines ignorées, non significatives quand `overrides` est
  vide) ; un `index` en double dans le tableau est refusé (`InvalidFile`) au
  lieu d'être fusionné champ à champ (le cadrage initial l'autorisait,
  décision revue — aucun producteur réel n'écrit jamais deux entrées pour le
  même index, cf. `docs/lot8-manual-editing-design.md` §4) ; une entrée sans
  aucune modification effective (ni `pos`, ni `type`, ni `trimAfter: true`)
  est également refusée. `effective_sequence` (enchaîne
  `generate_sequence` puis `apply_manual_overrides`) est vérifiée identique
  au brut pour un objet `Clean`, patchée pour un objet `ManuallyEdited`,
  déterministe sur deux appels indépendants, et propage sans plantage une
  erreur de génération (projet sans objet visible). Une garde CTest
  structurelle (`check_no_raw_sequence_bypass`) fait échouer la suite si un
  nouveau site de production appelle `generate_sequence()` sans passer par
  `effective_sequence()`. Toujours aucune UI dans ce sous-lot.
- **Retouches manuelles — mode d'édition des points, UI desktop (Lot 8.2)** :
  côté cœur, `is_movable_point` (prédicat d'éligibilité exporté, plus de
  duplication dans l'UI), `edit_view`/`classify_all_edit_states` (état dérivé
  + vue brute/empreinte/compteur pour un objet) et `refresh_context` (un seul
  `generate_sequence` interne pour produire à la fois la séquence effective —
  contenu **identique** à `effective_sequence`, mêmes deux passes —, les états
  par objet et la vue d'édition d'une cible optionnelle) sont vérifiés
  cohérents entre eux et propagent sans plantage une erreur de génération.
  Côté UI (QTest, offscreen, événements souris réels sur poignées visibles à
  deux niveaux de zoom, `QSignalSpy`) : activation/désactivation du mode
  selon la sélection et l'état `Dirty` (aucun second signal `toggled` émis
  par une sortie forcée, garde-fou vérifié via `QSignalSpy`) ; glisser une
  poignée exécute **une seule** `MoveStitchPointCommand` (undo ramène
  `canUndo()` à faux) dont l'annulation/le rétablissement restaurent
  exactement la même entrée d'override ; un clic sans déplacement ne crée
  aucune commande ; un changement de document pendant la fenêtre où la
  commande différée (`QTimer::singleShot(0)`, nécessaire pour ne pas détruire
  sa propre poignée) est en file d'attente fait abandonner cette commande
  sans muter le nouveau projet (régression du compteur `documentGeneration_`) ;
  abandonner les retouches d'un objet `Dirty` (confirmation auto-acceptée) est
  annulable et restaure exactement l'état antérieur ; un objet dont le nombre
  de points déplaçables dépasse le seuil d'affichage refuse proprement
  l'activation, avec message explicite en barre de statut, plutôt que de
  laisser le mode actif sans aucune poignée.
- **Tatami — correctif contacts sommet** : `segment_stays_in_region` détecte un
  connecteur **parfaitement vertical** traversant un trou en losange de part en
  part en touchant exactement ses deux sommets (défaut qu'une version antérieure
  laissait passer, faute de sonder l'intérieur pour un petit écart en x) ; même
  vérification avec **deux trous** (le couloir entre eux reste cousable) et sur
  le **coin rentrant** d'une forme en L (contact sommet vers l'encoche rejeté) ;
  non-régression : un suivi de bord colinéaire reste cousu ; `fill_tatami` sur
  un trou en losange ne produit aucun point cousu à l'intérieur.
- **Tatami — politique sûre de `tatami_underlay`** : un retrait de contour
  (`underlay_inset`) impossible (pièce trop petite) ne produit **aucune**
  sous-couche de contour (jamais de repli silencieux sur le bord brut) ; un
  retrait **explicitement nul** longe bien le bord brut (intention distincte).
- **Running** : espacement par longueur d'arc (cercle), coins préservés, courbes
  Bézier suivies, résultats déterministes.
- **Undo/redo** : « undo total = état initial », restauration exacte des labels ;
  aller-retour exact des commandes d'objet (`SetFillAngle`, `SetStitchType`,
  `SetStitchParams`, `ConvertFillsToTatami`, `SetCanvas`).
- **Projet** : aller-retour complet (image, ops, **cadre**, segmentation,
  tangentes, params).

## Ajouter un test

Ajoutez un `TEST_CASE` (nom **ASCII**) dans le `test_*.cpp` du module, ou un
nouveau fichier référencé dans le `CMakeLists.txt` du dossier de tests. Les golden
SVG ne sont **jamais** réécrits par les tests : ils se régénèrent explicitement
via `openstitch-cli stitchdebug`.

Pour l'UI desktop : ajoutez une fonction de test dans une suite existante de
`tests/unit/desktop/`, ou un nouveau fichier + `openstitch_add_qt_test(...)`
dans son `CMakeLists.txt`. Chaque test doit vérifier un résultat métier
observable (modèle, sélection, `enabled`/`checked`, valeur d'un signal) — pas
seulement l'absence de crash — et rester déterministe (pas de `sleep`, pas de
comparaison de pixels, pas de dépendance à la résolution/au thème).

## Implémentation associée

- `tests/` (arborescence complète), `tests/unit/*/CMakeLists.txt`.
- `tests/unit/desktop/` — tests Qt (QTest) de la couche UI.
- `apps/desktop/CMakeLists.txt` — bibliothèque `openstitch_desktop_widgets`.
- `CMakeLists.txt` (racine) — `enable_testing`, `Catch`.
