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
- Total au dernier passage vérifié : **250 tests CTest**, 100 % réussis.

## Encadré de traçabilité (dernier passage vérifié)

Un simple « 209/209 » devient vite périmé ; voici le contexte exact du dernier
passage vérifié manuellement. Régénérez ces valeurs avant toute publication.

| Élément | Valeur |
|---|---|
| Commit (état du code testé) | `0e396ab` |
| Compilateur | MSVC toolset 14.50 (Visual Studio 2026) |
| CMake | 4.4.0-rc3 |
| Configurations | Debug **et** Release |
| Résultat CTest | 250 / 250 réussis |
| Tests désactivés | 0 |
| Fichiers de tests d'intégration | 1 (`tests/integration/test_pipeline.cpp`) |
| Suites Qt (UI desktop) | 4 exécutables CTest (`tests/unit/desktop/`), 13 fonctions de test QTest |
| Tests sur machine réelle | 0 |
| Couverture de code | non mesurée |

Note : chaque `TEST_CASE` Catch2 (ou fonction de test QTest) est enregistré
comme un test CTest (via `catch_discover_tests` côté Catch2, `add_test` par
suite côté QTest) ; le nombre d'**assertions** est supérieur. Le chiffre 250
compte les cas de test, pas les assertions.

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
| stitch / stitch_generation | `test_stats.cpp`, `test_running_stitch.cpp`, `test_generate.cpp`, `test_tatami.cpp`, `test_satin.cpp`, `test_routing.cpp`, `test_overrides.cpp` |
| autodigitize | `test_autodigitize.cpp` |
| auto_satin | `tests/unit/auto_satin/test_pipeline.cpp`, `test_columns.cpp` |
| stitch_analysis | `test_analyze.cpp` |
| optimization | `test_order.cpp` |
| commands | `test_undo_stack.cpp` |
| formats | `test_dst.cpp`, `test_svg.cpp` |
| project_io | `test_roundtrip.cpp` |
| desktop (UI, Qt) | `tests/unit/desktop/test_canvas_view.cpp`, `test_node_handle.cpp`, `test_document_panel.cpp`, `test_properties_panel.cpp` |
| intégration | `tests/integration/test_pipeline.cpp` |

## Tests UI (Qt, desktop)

Fondation ajoutée pour vérifier réellement l'interface (et pas seulement le
cœur) : la sélection au canevas, le déplacement d'un nœud, la synchronisation
canevas/liste/inspecteur, sans jamais comparer de pixels ni dépendre d'un
écran.

### Architecture de testabilité

`apps/desktop/CMakeLists.txt` sépare les sources en une bibliothèque statique
`openstitch_desktop_widgets` (tout sauf `main.cpp`), liée à la fois par
l'exécutable `openstitch` et par les tests — c'est la seule refactorisation
faite pour la testabilité (aucun membre rendu public, aucune logique
déplacée). Chaque suite est un exécutable QTest (`QTEST_MAIN`), exécuté par
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

`MoveNodeCommand` (la commande undo/redo réellement appliquée quand une
poignée est relâchée) est déjà couverte côté cœur, sans Qt, par
`tests/unit/commands/test_undo_stack.cpp` (cas « AddVectorObject et
MoveNode ») — non dupliqué ici.

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

### Obstacles de testabilité identifiés (non résolus dans ce lot)

- **`MainWindow` est un god-object non testable en l'état** : constructeur de
  ~250 lignes construisant menus/docks/barres d'outils/panneaux en bloc,
  logique de sélection/filtre/simulation imbriquée dans des lambdas privées
  (`updateInspector`, `objectPassesFilter`, `embroideryCentroid`…), et surtout
  un effet de bord réel au **premier appel du constructeur** : `QSettings
  ("OpenStitch", "OpenStitch Studio")` lit/écrit la **même clé de registre
  Windows** que l'application réelle (`ui/geometry`, `ui/windowState`), donc
  un test qui instancierait `MainWindow` directement serait non déterministe
  (dépend de l'état laissé par une session utilisateur réelle) et polluerait
  ce registre. Aucun test de ce lot n'instancie `MainWindow`.
- **Sélection rectangulaire d'objets métier absente** : le `RubberBandDrag`
  actuel de `CanvasView` ne sert **que** le mode recadrage
  (`cropSelectedMm` → recadre l'image). Il n'existe aucun mécanisme qui
  sélectionne les objets vectoriels/de broderie contenus dans un rectangle
  glissé — fonctionnalité demandée par l'utilisateur mais pas encore codée.
  Rien n'a donc pu être testé « avec succès » sur ce point ; c'est une
  fonctionnalité **manquante**, pas un test manquant.
- **Déplacement global d'un objet absent** : seul le déplacement d'un nœud
  individuel existe (`NodeHandleItem` → `MoveNodeCommand`, testé). Il n'y a
  pas de commande ni d'interaction pour translater un objet entier (tous ses
  nœuds à la fois) ; pas de `MoveObjectCommand` dans `libs/commands`.
- **Actions contextuelles (`updateContextToolbar`, `updateActions`)** : câblées
  uniquement dans `MainWindow`, sur des `QAction*` membres privés sans
  `objectName`. Testables sans construire `MainWindow` en entier seulement
  après une extraction (ex. fonctions pures `objectPassesFilter`,
  `stitchTypeIndex`, `regionAreaMm2` déjà `static`/`const` — candidates à
  sortir dans un petit contrôleur non-Qt testable en Catch2).

### Roadmap (scénarios non automatisables aujourd'hui)

But visé par l'utilisateur, dans l'ordre de valeur/risque :

1. **Sélection rectangulaire d'objets** (prérequis produit avant tout test) :
   ajouter un mode canevas dédié (distinct du recadrage) qui émet les
   `ObjectId`/`RegionId` sous le rectangle ; puis test QTest reprenant le
   même schéma que `cropModeRubberBandEmitsCropSelectedMm`.
2. **Déplacement global d'un objet** : `MoveObjectCommand` (cœur, Catch2,
   translation de tous les nœuds + undo/redo — même schéma que
   `MoveNodeCommand`), puis interaction canevas (glisser l'intérieur d'un
   objet sélectionné) testée en QTest comme `NodeHandleItem`.
3. **Synchronisation sélection canevas ↔ liste ↔ inspecteur bout en bout** :
   aujourd'hui seule la synchronisation **descendante** (`DocumentPanel` :
   liste → signal, `syncSelection` sans réémission) est testée en isolation.
   Le cycle complet (clic canevas → `MainWindow::syncDocumentSelection` →
   `DocumentPanel::syncSelection` → `PropertiesPanel::showEmbroidery`)
   suppose d'extraire cette orchestration de `MainWindow` vers une classe
   testable (le god-object ci-dessus est le blocage).
4. **Undo/redo restaure modèle et UI** : le modèle est déjà couvert (Catch2,
   `test_undo_stack.cpp`) ; il manque la vérification que l'UI (inspecteur,
   liste, canevas) se **rafraîchit** correctement après un undo déclenché
   depuis le menu — bloqué par le même god-object `MainWindow`.
5. **Menus/actions selon l'état** (`updateActions`) : nécessite soit
   d'instancier `MainWindow` (obstacle `QSettings` ci-dessus à lever d'abord
   — ex. permettre d'injecter un nom d'organisation/application différent en
   test), soit d'extraire la logique d'activation dans un contrôleur pur.

## Types de vérifications notables

- **DST aller-retour** sur tous les deltas de −121 à +121, déterminisme octet à
  octet.
- **Tatami** : invariant « aucune couture ne traverse le trou » d'un anneau, peu
  de déplacements ; et « aucune couture hors région » sur une forme concave en
  **L** échantillonnée à cinq angles (filet anti-débordement).
- **Auto-numérisation** : une bande fine devient un **tatami** par défaut (satin
  naïf désactivé), et un satin uniquement si `use_naive_satin` est activé.
- **Auto-satin géométrique** (`build_satin_columns`) : formes simples produisent
  une colonne, milieu des barreaux **intérieur à la région**, Y → plusieurs
  colonnes, cercle/anneau/large **refusés**, déterminisme des rails ; SVG dans
  `tests/golden/auto-satin/`.
- **Satin par barreaux** (`fill_satin_columns`) : espacement **médian régulier**
  (colonne droite), barreaux **traversés exactement**, rails de longueurs
  différentes, repli sur `fill_satin` si < 2 barreaux, déterminisme.
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
  longue → saut) ; à la génération, un groupe adjacent n'émet qu'un saut initial
  (le reste cousu en passe `Travel`), un groupe éloigné conserve ses sauts.
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
