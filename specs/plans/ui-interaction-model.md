# Plan : lot L5 — modèle d'interaction souris/clavier et menu Aide

Status: revision 2 (revue de conception : APPROVE WITH CHANGES, décisions du Lead intégrées — voir « Revision 2 » en fin de document)
Référence : `docs/ui-audit-2026-10.md` §3 bis, §4, §5. Public : relecteur de conception, codeurs, testeur.

## 0. État réel du dépôt (vérifié par grep, à connaître avant de coder)

- `CanvasView` ne gère que le bouton gauche. `setDragMode(ScrollHandDrag)` par défaut (`canvas_view.cpp:37`, `:99`) ;
  `updateDragMode()` (`:93-101`) ne retourne `NoDrag` que pour polygone/freeform/satin/bézier, `RubberBandDrag`
  pour recadrage et cadre élastique. `wheelEvent` (`:186-192`) lit `angleDelta().y()` seul.
- `applyZoom(factor, anchorUnderMouse)` (`:173-184`) s'appuie sur `AnchorUnderMouse`, qui lit `QCursor::pos()` et non la
  position de l'évènement : faux avec des évènements synthétiques et avec un pavé tactile. À remplacer par un ancrage manuel (§2.4).
- `canvasClickedMm(QPointF)` est émis **à l'appui** (`:212-214`), sans modificateurs. `boxDrawnMm` porte déjà les
  modificateurs lus sur l'évènement de relâchement (`:253`, `:270`) : c'est le modèle à généraliser.
- **La sélection est mono-objet** : `std::optional<RegionId> selectedRegion_`, `std::optional<ObjectId> selectedObject_`,
  `selectedEmbroidery_` (`main_window.hpp:572-574`, 53 occurrences de `selectedObject_`, ~21 sites d'écriture dans `main_window.cpp` : `:827-829, :1862-1864, :2122-2124, :2144-2147, :3111, :3184, :3298, :4523-4525, :4540-4542, :4567, :4625-4642, :4673-4675, :4749-4751, :5641-5655, :5717, :5898-5903, :6759-6761, :6799-6825, :6847, :6869`).
  Maj/Ctrl+clic et le rectangle multi-objets exigent un **ensemble** de sélection avec un mutateur unique (§2.7) — c'est le vrai coût de L5.
- **Outil Pan** : `Tool::Pan` (`main_window.cpp:5314`, `:5509`) compte aujourd'hui sur `ScrollHandDrag` (le clic y est ignoré, `onCanvasClicked` `:6693`). Passer à `NoDrag` sans ligne de table dédiée le casserait (§1.6 `P1`, §2.1).
- **Tests déjà présents** (`tests/unit/desktop/test_canvas_input.cpp`, 400 lignes, et `test_ui_characterization.cpp`, 625 lignes) : voir §5.2 ; notamment `leftDragInEmptyAreaPansTheViewAndStillEmitsOneClick` et `shiftDragInEmptyAreaStillPans` figent le comportement `ScrollHandDrag` actuel.
- **Plates-formes (Windows)** : Qt 6 sous Windows livre la molette en `angleDelta` seul (deltas possiblement < 120), le pincement du pavé tactile arrive comme **Ctrl + molette**, sans `pixelDelta` ni `QNativeGestureEvent`. Ces deux-là ne sont donc livrés qu'en macOS / Linux-Wayland (G8, G9).
- `onCanvasClicked` (`main_window.cpp:6691-6862`) porte toute la logique de sélection (objets vectoriels via
  `objectPainterPath(object).contains(posMm)`, satin via `satinEmbroideryAt`, régions via `segmentation::region_at`).
- Suppr : `delRegionAct->setShortcut(QKeySequence::Delete)` (`:611`) ne supprime que la région (`deleteSelectedRegion`, `:6864`).
  « Suppr universel » est **déjà au périmètre de L1** (audit §5) : L5 ne le réécrit pas, il l'étend à l'ensemble (T4).
- Aide : `buildHelpMenu()` (`main_window.cpp:5062-5090`) = un `QMessageBox` en dur périmé + « À propos ». Touche **F** =
  `QShortcut` (`:5065`), Échap/Entrée/Retour/Retour arrière = `QShortcut` (`:5386-5430`) : sans `QAction`, donc invisibles
  d'une génération « depuis les actions » → ils entrent par la table (§1).
- Barre d'état : `toolLabel_`, `cursorLabel_` en `addPermanentWidget` (`:407-410`) ; 87 `showMessage` sans délai partout.
  Un widget ajouté par `addWidget` serait **masqué** pendant tout `showMessage` : la ligne d'indications doit être un widget **permanent**.
- Qt de dev = 6.4.2 (Linux), CI = 6.8.3 : `QStyleHints::mousePressAndHoldInterval()` (6.5+) est **indisponible** → délai propre (§1.4).
- Qt-light : `interaction_map` = **Core + Gui, jamais Widgets** (`QPainterPath`/`QRectF` pour `rectSelects`, enums `Qt::`, `QString`, `QList`, `QSettings`).
- `libs/commands` ne contient que `ICommand` + `UndoStack` (`libs/commands/include/openstitch/commands/command.hpp`) : pas de commande composite aujourd'hui.

## 1. Modèle de données : `InteractionMap` (`apps/desktop/interaction_map.{hpp,cpp}`)

### 1.1 Types (namespace `openstitch::desktop`)

```cpp
enum class Context : uint8_t {
  Global,      // repli : s'applique si le contexte actif n'a pas de ligne pour ce geste (SAUF contextMenuEvent, cf. 1.6)
  Select,      // outil Sélection, geste sur le vide ou sur un objet non déplacé
  Pan,         // outil « Déplacer la vue » : glisser gauche = panoramique
  Move,        // glisser un objet/forme sélectionné (corps de VectorObjectBodyItem)
  Crop,        // outil Rectangle / recadrage
  DrawBox,     // DrawRectangle, DrawEllipse, DrawPolygonRegular (cadre glissé)
  DrawClicks,  // DrawPolygon, DrawSatinColumn, DrawDirectionGuide, DrawBreakLine (clics successifs)
  DrawBezier,  // DrawBezier, DrawSatinCutLine (clic / clic-glisser)
  DrawFreeform,
  NodeEdit,    // poignées de nœuds / rails (NodeHandleItem)
  StitchEdit   // édition des points de couture (Lot 8.2)
};
enum class GestureKind : uint8_t { Click, DoubleClick, LongPress, Drag, Wheel, PixelScroll, NativePinch, Hover, Key };
struct Gesture {
  GestureKind kind;
  Qt::MouseButton button{Qt::NoButton};      // Click/DoubleClick/LongPress/Drag
  Qt::KeyboardModifiers mods{};              // lus sur l'évènement, jamais QGuiApplication::keyboardModifiers()
  Qt::Key key{Qt::Key(0)};                   // Key ; ou touche MAINTENUE pour Drag (Espace + glisser)
  bool operator==(const Gesture&) const = default;
};
enum class Intent : uint8_t {
  ZoomAtCursor, ZoomDrag, PanView, FitDesign, ScrollHorizontal, ScrollVertical,
  SelectReplace, SelectAdd, SelectToggle, SelectBelow, SelectRectangle, EnterEdit, HoverHighlight,
  FitCanvas, NudgeFine, NudgeCoarse,
  MoveObject, AxisLock, SuspendSnap, DuplicateOnMove,
  DrawPoint, FinishDraw, ConstrainShape, DrawFromCenter, RemoveLastPoint, CancelTool,
  MoveNode, InsertNode, DeleteNodes, DeleteSelection, ContextMenu,
  SelectModeWindow, SelectModeFree, SelectModeBrush      // 2/3 : Planned (voir 1.2)
};
enum class Origin : uint8_t { Fusion, Proposal, Existing };   // Existing = déjà dans le code, ajouté par l'architecte
enum class Preset : uint8_t { OpenStitch, Touchpad };
struct Row {
  const char* id;            // « G3 », stable, sert de tag de test et d'ancre dans la doc
  Context context; Gesture gesture; Intent intent;
  const char* labelKey;      // QT_TRANSLATE_NOOP("InteractionMap", "Panoramique")
  Origin origin;
  uint8_t presets;           // masque : bit0 OpenStitch, bit1 Touchpad
  bool routed;               // true = CanvasView passe par resolve() ; false = documentée, exécutée par du code existant
  bool planned;              // true = jamais listée dans l'aide / les hints / la doc tant que non livrée
  bool overridesGlobal;      // true = autorisée à masquer une ligne Global de même geste
  bool hint;                 // true = apparaît dans la ligne d'indications sans modificateur tenu
};
struct Hint { QString gesture; QString label; };
```

### 1.2 API (toutes `static`, table `constexpr std::array<Row, N>` unique dans le `.cpp`, aucun état hors preset)

```cpp
class InteractionMap {
public:
  static std::optional<Intent> resolve(Context, const Gesture&);          // exact sur (button, mods, key), spécifique puis Global
  static QList<Hint> hintsFor(Context, Qt::KeyboardModifiers held);        // 1.3
  static QList<const Row*> allRows(bool includePlanned = false);           // ordre de la table, filtré par preset actif
  static QString describe(const Gesture&);                                 // « Ctrl + Maj + clic molette + glisser » (FR, source non traduite si pas de traducteur)
  static QString contextName(Context);
  static void setLongPressMsForTesting(int ms);                            // surcharge test-only ; -1 = retour à la valeur normale
  static Context contextFor(Tool, bool nodeEditActive, bool stitchEditActive);   // inclut tools.hpp (Qt-free)
  static bool rectSelects(const QRectF& rect, const QPainterPath& shape, bool crossing);   // fenêtre : boundingRect entièrement dans rect ; croisement : shape.intersects(rect)
  static Preset preset();  static void setPreset(Preset);  static Preset loadPreset();  static void savePreset();
  static constexpr int kLongPressMs = 500;                                 // 1.4
  static int longPressMs();                                                // QSettings « navigation/longPressMs », borné 300-1000, défaut kLongPressMs
};
```
Les lignes `planned` (touches 2/3 : lasso libre, pinceau) sont dans la table pour mémoire mais exclues d'`allRows()`, des hints,
de la doc et de `resolve()` tant que non livrées (ne pas documenter ce qui n'existe pas). La touche 1 est le mode par défaut, sans ligne.

### 1.3 `hintsFor`
- Sans modificateur tenu : lignes du contexte puis `Global` avec `hint == true` (6 au plus, ordre de la table).
- Avec `held` non vide : lignes (contexte + Global, preset actif, non planned) dont `gesture.mods` ⊇ `held`, formatées
  « Maj + clic : ajouter ». Le widget de la ligne d'état n'affiche que ce que le modificateur tenu **ferait dans l'état courant**.

### 1.4 Appui long
Constante `kLongPressMs = 500` (valeur **proposée**, la valeur Fusion n'est pas documentée — audit « Non vérifié »), surchargeable par
`QSettings navigation/longPressMs`, bornée 300-1000. Un setter **test-only** `InteractionMap::setLongPressMsForTesting(int)` prime sur tout ; la lecture `QSettings` utilise le constructeur `QSettings(org, app)` (jamais un chemin figé), donc respecte le `QSettings::setPath`/organisation temporaires des tests. Annulé si le curseur s'éloigne de plus de `QApplication::startDragDistance()`
ou si le bouton est relâché avant l'échéance. Implémenté par un `QTimer` à tir unique dans `CanvasView` (pas `mousePressAndHoldInterval`, cf. §0).

### 1.5 Règles de conflit (testées, §5)
1. Clé `(context, gesture)` **unique** dans la table (hors filtre de preset ; dans un preset donné aussi).
2. Une ligne d'un contexte spécifique ne peut masquer une ligne `Global` de même geste que si `overridesGlobal` (ex. Suppr en `NodeEdit`).
3. La correspondance est **exacte** sur les modificateurs : Maj+clic ≠ clic. Les conflits « Maj = ajout (rectangle) / verrou d'axe (déplacement) /
   contrainte (dessin) » sont levés **par contexte** (`Select` / `Move` / `Draw*`), jamais par priorité implicite.
4. Chaque `id` unique ; chaque ligne `routed` a une `Intent` traitée par `CanvasView` (test d'exhaustivité du `switch`, `-Wswitch` en erreur).
5. Conflit avec les `QAction`/`QShortcut` de la fenêtre : toute ligne `Key` **routée** (`routed == true`) ne doit coïncider avec aucune séquence de `QAction::shortcuts()` ni de `QShortcut::key()` (test `routedKeyRowsDoNotClashWithWindowShortcuts`, qui énumère comme `windowShortcutsAreUnique`, `test_ui_invariants.cpp:76`). Sont **exemptées** les lignes `routed == false` (`Existing` : F, Échap, Entrée, Retour arrière, Suppr, flèches, qui *sont* ces raccourcis déjà câblés) ; elles sont à l'inverse vérifiées dans l'autre sens : `existingKeyRowsMatchAnActualShortcut` (la touche documentée existe bien comme `QAction`/`QShortcut` ou comme `keyPressEvent` du canevas, liste blanche courte `{flèches}`).

### 1.6 Lignes de la table (D = origine ; F = Fusion confirmé, P = proposition OpenStitch, E = existant ; R = routée)
Libellés = ceux de l'audit §3 bis. Origines F/P reprises à l'identique de « Sources et degré de confirmation » ; E est ajouté ici.

| id | Contexte | Geste | Intent | D | R |
|---|---|---|---|---|---|
| G1 | Global | Molette | ZoomAtCursor | F | oui |
| G2 | Global | Ctrl+Maj + glisser bouton milieu | ZoomDrag | F (variante) | oui |
| G3 | Global | Glisser bouton milieu | PanView | F | oui |
| G4 | Global | Double-clic bouton milieu | FitDesign | P | oui |
| G5 | Global | Espace (tenu) + glisser gauche | PanView | P | oui |
| G6 | Global | Maj + molette | ScrollHorizontal | P | oui |
| G7 | Global | Alt + molette | ScrollVertical | P | oui |
| G8 | Global | Défilement 2 doigts (`pixelDelta`) | PanView | P | oui |
| G9 | Global | Pincement (`QNativeGestureEvent`) | ZoomAtCursor | P | oui |
| G10 | Select/Move/NodeEdit/StitchEdit | Clic droit | ContextMenu | E | non (existe : `contextMenuEvent`, `:225`) |
| G11 | Global | Suppr | DeleteSelection | E (régions seules aujourd'hui ; **universel livré en T4 sur la multi-sélection**, L1 n'y touche plus) | non |
| G12 | Global | Échap | CancelTool | E | non |
| G13 | Global | Ctrl + molette | ZoomAtCursor | P (**dans les deux préréglages** : pincement Windows) | oui |
| G14 | Global | Touche F | FitCanvas | E | non (`QShortcut` `:5065`) |
| G15 | Global | Flèches / Maj + flèches | NudgeFine 0,1 mm / NudgeCoarse 1 mm | E | non (`canvas_view.cpp:275-300`, objet principal seul) |
| P1 | Pan | Glisser gauche | PanView | E (outil « Déplacer la vue ») | oui |
| S1 | Select | Clic (sur le vide : désélectionne) | SelectReplace | E | oui |
| S2 | Select | Maj + clic | SelectAdd | F | oui |
| S3 | Select | Ctrl + clic | SelectToggle | F | oui |
| S4 | Select | Appui long gauche | SelectBelow | F | oui |
| S5 | Select | Alt + clic | SelectBelow | P | oui |
| S6 | Select | Glisser dans le vide (G→D englobe, D→G croise) | SelectRectangle | F | oui |
| S7 | Select | Maj + glisser | SelectRectangle (ajout seul) | F | oui |
| S8 | Select | Ctrl + glisser | SelectRectangle (bascule) | F | oui |
| S9 | Select | Touche 2 / 3 | SelectModeFree / Brush | F (esquisse) | planned |
| S10 | Select | Double-clic objet | EnterEdit | P | non (existant : `onCanvasDoubleClicked`) |
| S11 | Select | Survol | HoverHighlight | P | oui |
| M1 | Move | Glisser un objet | MoveObject | E | non (`VectorObjectBodyItem`) |
| M2 | Move | Maj + glisser | AxisLock | P | non → T4 |
| M3 | Move | Ctrl + glisser | SuspendSnap | P | non → T4 |
| M4 | Move | Alt + glisser | DuplicateOnMove | P | non → T4 |
| D1 | DrawClicks | Clic | DrawPoint | E | non |
| D2 | DrawClicks | Double-clic ou Entrée | FinishDraw | E | non |
| D3 | DrawBox | Maj | ConstrainShape (cercle/carré) | E (ellipse) ; angle 15° P | non |
| D4 | DrawBox | Alt | DrawFromCenter | P | non → T4 |
| D5 | DrawClicks | Ctrl (tenu) | SuspendSnap | P | non |
| D6 | DrawClicks | Retour arrière | RemoveLastPoint | E | non |
| D7 | DrawClicks | Échap | CancelTool | E | non |
| N1 | NodeEdit | Glisser un nœud | MoveNode | E | non |
| N2 | NodeEdit | Maj / Ctrl + glisser | AxisLock / SuspendSnap | P | non → T4 |
| N3 | NodeEdit | Double-clic sur un segment | InsertNode | P | non (hors L5 si l'insertion n'existe pas : ligne `planned`) |
| N4 | NodeEdit | Suppr | DeleteNodes (`overridesGlobal`) | E (menu « Supprimer le nœud », `:2354`) | non |

Les lignes `Draw*` non routées servent l'aide et les hints ; leur exécution reste celle de `MainWindow`. Seules les lignes `routed` flippent des tests de comportement.
Variantes par contexte : les gestes `Global` de **souris/molette/touche d'Espace** (G1-G9, G13) sont valables dans tous les contextes, donc le panoramique reste possible pendant un outil de dessin (exigence de l'audit). **Exception** : G10 (clic droit) ne l'est pas : `contextMenuEvent` retourne tôt en recadrage/dessin (`canvas_view.cpp:226-229`) ; la ligne G10 est donc `Select`/`Move`/`NodeEdit`/`StitchEdit` et non `Global`. Les lignes clavier (G11, G12, G14, G15) sont `Global` mais `routed=false`.
Avertissement G11 : la ligne documente le comportement final (Suppr sur tout type d'objet) ; sa livraison est en T4 (tests `test_ui_characterization.cpp:362,:378` à retourner).

### 1.7 Préréglage « Touchpad / sans clic molette » (altère la table par masque, pas par seconde table)
- Masque `presets` : G2, G3, G4 = OpenStitch seul (pas de bouton milieu). G5 (Espace+glisser) devient la ligne `hint` principale du panoramique. **G13 (Ctrl+molette = zoom) est dans les deux préréglages** (c'est ainsi que Windows livre le pincement).
- G1 : une molette sans `pixelDelta` zoome dans les deux préréglages ; avec `pixelDelta` non nul (macOS/Wayland), sans Ctrl, c'est un **pan** (G8) dans les deux.
- **Limite documentée (Windows)** : sans `pixelDelta` ni gestes natifs, Qt ne permet pas de distinguer un défilement de pavé tactile d'une molette ; le préréglage Touchpad n'y change donc que l'affichage (G2-G4 masquées, Espace+glisser et Ctrl+molette mis en avant) ; le panoramique y est **Espace + glisser**. G8 et G9 ne sont **pas** des critères d'acceptation Windows (macOS / Linux-Wayland seulement ; tests par évènements injectés).
- Persistance : `QSettings(QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"))` (même couple que `app_theme.cpp:12`), clé `navigation/preset` = `openstitch|touchpad`.
  `setPreset()` n'écrit pas ; `savePreset()` écrit. UI : sous-menu **Affichage ▸ Navigation** (`QActionGroup` exclusif, 2 choix), `objectName` `navPresetOpenStitch|Touchpad` (T4).
  Pas de préférences générales avant L6.

## 2. Changements de `CanvasView` (`canvas_view.{hpp,cpp}`) — propriétaire T2

Principe : `CanvasView` traduit les évènements Qt en `Gesture` (modificateurs lus sur `QMouseEvent/QWheelEvent/QKeyEvent`),
appelle `InteractionMap::resolve(currentContext(), g)` et exécute les intents `routed`. Aucune mutation du `Project` : tout ce qui touche à la
sélection sort en **signaux** ; `MainWindow` décide, via ses chemins et commandes.

### 2.1 Mode de glisser et contexte (aucune demi-migration)
- **Un `CanvasView` isolé (aucun appel de `setBaseContext`) garde `ScrollHandDrag` par défaut** : `test_canvas_input.cpp` (`leftDragInEmptyAreaPansTheViewAndStillEmitsOneClick`, `shiftDragInEmptyAreaStillPans`) et `test_canvas_view.cpp` restent verts à l'identique après T2.
- `bool inputModelEnabled_{false}` posé à `true` par le **premier** `setBaseContext(Context)` (appelé par `MainWindow::setTool`/`setBaseContext` en T4). Tant qu'il est faux : `updateDragMode()` (`:93-101`) inchangé, aucune des nouvelles branches ci-dessous n'est active sauf celles qui ne font que *s'ajouter* (clic milieu, molette Maj/Alt/Ctrl/pixelDelta, Espace, gestes natifs : aucun test existant ne les attend absents hors les `QEXPECT_FAIL`).
  Quand il est vrai : `updateDragMode()` → `NoDrag` dans la branche `else` ; `RubberBandDrag` conservé pour `cropMode_/boxDrawMode_` (`rubberBandChanged` et `lastRubberBandMm_` inchangés).
  **T2 et T4 sont livrés dans un même commit** (ou T2 reste inerte comme ci-dessus jusqu'à T4) : l'application n'est jamais à moitié migrée.
- `Context baseContext_{Select}` ; `currentContext()` privé : dérive Crop/DrawBox/DrawClicks/DrawBezier/DrawFreeform des six booléens existants, sinon `baseContext_` (Select, **Pan**, NodeEdit, StitchEdit).
- **Outil Pan** (`Tool::Pan`) : `MainWindow::setTool` passe `setBaseContext(Pan)`. Branche dédiée dans `mousePressEvent` : contexte `Pan` + bouton gauche → `PanView` (ligne P1), sans émettre `canvasClickedMm` (le clic reste ignoré par `onCanvasClicked` `:6693` en double sécurité). Test : `panToolLeftDragPansWithNoDragMode`.
- `setSelectionRectangleEnabled(bool)` (défaut false) n'est vrai qu'en contexte `Select`.

### 2.2 Panoramique (G3, G5, G8, P1) — disponible dans tout contexte
- `mousePressEvent` (`:194`) : **avant** les branches freeform/bézier, si `button==Middle`, ou `Left && spaceHeld_`, ou contexte `Pan`+Left → `panning_=true`, mémorise la position viewport, curseur main fermée, `event->accept()`, ni base ni `canvasClickedMm`.
- `mouseMoveEvent` (`:234`) : si `panning_`, `hScrollBar()->setValue(h - dx)` / `vScrollBar()->setValue(v - dy)` (→ `scrollContentsBy` → `viewChanged`, `:307`) ; `cursorMovedMm` reste émis. `mouseReleaseEvent` (`:247`) : fin du pan, curseur du contexte restauré.
- Un pan en cours n'émet ni `freeformPointMm` ni `bezierPointDraggingMm` (test : pan pendant `setFreeformDrawMode(true)`).
- Séquence Qt d'un double-clic milieu (press / release / **dblclick** / release) : `mouseDoubleClickEvent` bouton milieu → `fitCanvas()` et `middleDoubleClickPending_` pour avaler le `release` suivant (pas de pan fantôme ni de second ajustement) ; le premier press/release de la séquence ne déplace pas la vue (déplacement nul).

### 2.3 Espace et focus (G5) — filtre applicatif, pas de vol de focus
- `keyPressEvent` (`:275`) : `Key_Space` non auto-répété → `spaceHeld_=true`, curseur main ouverte, `accept`, sans appeler la base. `keyReleaseEvent` (nouveau) relâche. Remise à zéro dans `focusOutEvent`, `hideEvent`, et `changeEvent(ActivationChange)` quand la fenêtre devient inactive.
- **Pas de `setFocus` dans `enterEvent`.** À la place, un `QObject` filtre applicatif (`SpaceKeyFilter`, installé via `qApp->installEventFilter` par `CanvasView`, retiré dans le destructeur) intercepte `KeyPress/KeyRelease` d'Espace **seulement si** le curseur est au-dessus du viewport (`viewport()->underMouse()`) **et** `QApplication::focusWidget()` n'est pas un champ de saisie (`QLineEdit`, `QAbstractSpinBox`, `QTextEdit`, `QComboBox` éditable) ; il consomme alors l'évènement (un `QPushButton`/`QToolButton` focalisé ne reçoit donc jamais le « clic » d'Espace) et règle `spaceHeld_`. Hors de ces conditions, l'évènement est transmis tel quel.
- Tests : `QEnterEvent` injecté sur le viewport (le survol `underMouse()` réel n'existe pas en offscreen : le filtre lit un drapeau `cursorOverViewport_` posé par `enterEvent`/`leaveEvent`, que le test pilote) ; `spaceDoesNotClickFocusedButton` ; `spaceIgnoredWhenSpinBoxHasFocus`.
- **Alt seul** (activation de la barre de menus sous Windows au relâchement) : le même filtre capte le `KeyRelease` d'`Alt` quand le curseur est sur le viewport et qu'un Alt+clic/Alt+molette a eu lieu depuis le dernier appui (`altUsedInGesture_`), et l'avale (`return true`). Vérification manuelle Windows en plus (la CI offscreen ne reproduit pas la barre de menus).

### 2.4 Molette, trackpad, gestes natifs (G1, G6-G9, G13)
- Remplacer `applyZoom(factor, anchorUnderMouse)` (`:173-184`) par `zoomAt(double factor, QPointF viewportPos)` : `setTransformationAnchor(NoAnchor)` ; `scenePos = mapToScene(viewportPos)`, `scale(f,f)`, puis ajustement des scrollbars pour que `scenePos` revienne sous `viewportPos`. `zoomIn/zoomOut` (`:158-164`) gardent l'ancrage centre. Bornes `kMin/kMaxPxPerMm` inchangées.
- **Zoom lissé** : `factor = pow(kZoomStep, clamp(deltaY / 120.0, -3.0, 3.0))` : accepte les deltas fractionnaires (< 120, pavés tactiles Windows) et plafonne un coup de molette violent.
- `wheelEvent` (`:186`) : `delta = pixelDelta non nul ? pixelDelta : angleDelta` ; si `angleDelta().y()==0` utiliser `angleDelta().x()` (Qt échange x/y avec Alt sur certaines plates-formes). Résolution : Ctrl → ZoomAtCursor (G13), `Shift` → ScrollHorizontal, `Alt` → ScrollVertical, `pixelDelta` non nul → PanView (G8), sinon ZoomAtCursor (G1) sur `event->position()`. `accept()` toujours. Alt+molette marque `altUsedInGesture_`.
- `bool viewportEvent(QEvent*) override` (nouveau) : `QEvent::NativeGesture` — `ZoomNativeGesture` → `zoomAt(1.0 + value(), position)`, `PanNativeGesture` → pan par `delta`, `SmartZoomNativeGesture` → `fitCanvas()`. Pas de `QPinchGesture`. (macOS / Linux-Wayland ; sous Windows, G13.)
- Zoom continu G2 : glisser milieu avec Ctrl+Maj, `factor = exp(-dy * 0.01)` ancré au point d'appui. Double-clic milieu G4 : cf. 2.2.

### 2.5 Sélection (S1-S8) — activée par `setSelectionRectangleEnabled(true)` (contexte `Select`)
- `enum class SelectMode { Replace, Add, Toggle };` (dans `interaction_map.hpp`). Aucun modificateur → Replace ; **Maj → Add** ; **Ctrl → Toggle** ; Maj+Ctrl → Toggle (documenté, testé).
- `mousePressEvent` : bouton gauche sur le vide **ou sur un objet** → mémorise `pressViewportPos_`, `pressMods_`, démarre le timer d'appui long (§1.4), **ne rien émettre** (clic différé au relâchement). Garde `onInteractiveItem` (`:209-211`) conservée **pour les poignées** (`NodeHandleItem`, `ResizeHandleItem`).
  **Cas du `VectorObjectBodyItem` sélectionné (`ItemIsMovable`)** : si un modificateur (Maj/Ctrl/Alt) est tenu au press, `CanvasView` traite le geste comme une sélection (le press n'est **pas** transmis à la base : pas de glisser d'item) ; sans modificateur, le press est transmis à l'item (déplacement M1 inchangé). Test : `shiftPressOnSelectedBodyTogglesInsteadOfDragging`.
- `mouseMoveEvent` : distance > `startDragDistance()` → annule l'appui long, passe en rectangle ; un `QRubberBand` enfant du viewport (créé paresseusement) montre le cadre ; **couleurs via un nouveau token `canvasSelectionRect`** (propriétaire T2 : `design_tokens.{hpp,cpp}` ajoutés à sa liste de fichiers ; QSS `QRubberBand` par `app_theme` si nécessaire, sinon palette posée dans `CanvasView`). Sens lu **au relâchement** : `crossing = releasePos.x() < pressPos.x()`.
- `mouseReleaseEvent` : rectangle → `selectionRectangleMm(rect, mode, crossing)` ; clic → `canvasClickedMm(pos)` si Replace (**connexions existantes**) sinon `selectionClickedMm(pos, mode)`. Appui long ou Alt+clic → `selectBelowRequested(pos, globalPos, mode)` ; l'appui long consommé n'émet ni clic ni rectangle.
- Survol (S11) : `cursorMovedMm` existant ; `MainWindow` calcule la surbrillance (§2.7), coalescence 16 ms si mesuré lent. Crop/Draw* inchangés (`canvasClickedMm` y reste émis à l'appui : le polygone et la colonne satin en dépendent).

### 2.6 Curseurs et modificateurs
- `updateModifierCursor(mods)` : `Select`+Maj → « + » ; +Ctrl → « ± » ; `Move`+Alt → « copie » ; `spaceHeld_` → main ouverte ; pan actif → main fermée. Pixmaps 24×24 dessinés par code, une fois, recolorés par token, posés via `applyModeCursor` (`:71`, vue **et** viewport).
- Modificateurs lus dans `mouseMoveEvent`, `keyPress/Release`, `enterEvent` (`event->modifiers()`), **jamais** `QGuiApplication::keyboardModifiers()` (test : un `QKeyEvent` d'appui sur Maj porte `ShiftModifier`, Qt 6.4 et 6.8).
- Signal `modifiersChanged(Qt::KeyboardModifiers)`, émis seulement sur changement.

### 2.7 Signaux, sélection multiple et câblage `MainWindow` (T4)
```cpp
void selectionRectangleMm(QRectF rectMm, SelectMode mode, bool crossing);
void selectionClickedMm(QPointF posMm, SelectMode mode);            // seulement mode != Replace
void selectBelowRequested(QPointF posMm, QPoint globalPos, SelectMode mode);
void modifiersChanged(Qt::KeyboardModifiers);
```
**Sélection multiple (décision du Lead)** : `std::vector<ObjectId> multiSelection_` (objets vectoriels), **vide dans le cas legacy** mono-sélection ; l'objet principal = `multiSelection_.back()` quand non vide, et `selectedObject_` reste le principal reflété (aucune lecture existante ne change).
- **Un seul mutateur** : `void setSelection(Selection)` (`struct Selection { std::optional<RegionId> region; std::optional<ObjectId> embroidery; std::vector<ObjectId> objects; }`) + `clearMultiSelection()`. **Tous les ~21 sites d'écriture** (liste §0 : `:827, :1862, :2122, :2144, :3111, :3184, :3298, :4523, :4540, :4567, :4625-4642, :4673, :4749, :5641-5655, :5717, :5898-5903, :6759, :6799-6825, :6847, :6869`) passent par lui ; un test structurel CTest (`grep`, comme `check_no_raw_sequence_bypass.cmake`) échoue si `selectedObject_ =`/`selectedRegion_ =`/`.reset()` apparaissent hors du mutateur.
- **Invariants** (`checkSelectionInvariants()` appelée en `Q_ASSERT` et testée) : le principal n'apparaît qu'une fois ; tous les ids existent dans `project_.vector_objects` ; sélectionner une région ou une broderie vide `multiSelection_` ; `refreshImage()` (`:~2146`) **élague** les ids disparus (suppression, undo, redo, rechargement) puis re-promeut le dernier restant.
- Basculer (Ctrl) le principal le retire et **promeut l'élément précédent** ; Add d'un objet déjà présent ne fait rien ; Toggle d'un absent l'ajoute en dernier (devient principal).
- Avec > 1 objet : `updateActions()` désactive les actions mono-objet (créer satin/tatami/contour, auto-satin, remodelage, orientation) et les flèches déplacent **tout l'ensemble** (une `CompositeCommand` de `TranslateVectorObjectCommand`) ; l'inspecteur affiche « N objets » (texte seul, pas d'édition).
- **`CompositeCommand`** : `libs/commands` (Qt-free), livrée en **T1** avec `tests/unit/commands/test_composite_command.cpp` (voir §6). Suppr universel (T4, `deleteSelection()`) construit une `CompositeCommand` sur la multi-sélection (un seul pas d'undo, libellé « Supprimer N objets »).
- `selectAt(QPointF, SelectMode)` : extraction du corps vectoriel/satin/région de `onCanvasClicked` (`:6750-6862`) ; `onCanvasClicked(pos)` = `selectAt(pos, Replace)` (non-régression). `onSelectionRectangle` : `rectSelects(rect, objectPainterPath(o), crossing)` par objet visible. `onSelectBelow` : `QMenu` des objets sous `pos` ; choix → `selectAt`. Régions/broderie : mono-sélection (Maj/Ctrl = Replace, documenté).
- Modificateurs de déplacement/dessin (M2-M4, N2, D4), **en T4** : dans le lambda `VectorObjectBodyItem` (`main_window.cpp:~2281`, `TranslateVectorObjectCommand`) : Maj → delta projeté sur l'axe dominant, Ctrl → saute `findSnapPointMm`, Alt → `DuplicateOnMove` = copie (`duplicateObject`, `:~4673`) puis translation de la copie ; `NodeHandleItem` / `VectorObjectBodyItem` passent `Qt::KeyboardModifiers` (surcharge compatible, `node_handle.hpp`, **propriétaire T4** désormais) ; `onBoxDrawn` lit Alt (déjà reçu en `modifiers`, `:1435-1444`) pour D4. Tests : `shiftDragMovesAlongDominantAxis`, `ctrlDragSkipsSnap`, `altDragDuplicatesAndMovesCopy`, `altBoxDrawGrowsFromCenter`.
- `setTool` (`:5442`) : `setBaseContext(...)`, `setSelectionRectangleEnabled(tool == Tool::Select)`, rafraîchit la ligne d'indications ; modes d'édition (`satinEditModeAct_`, `stitchEditModeAct_`, `railEditModeAct_`) → `setBaseContext(NodeEdit|StitchEdit)`. Le lambda `cursorMovedMm` (`:419-440`) calcule la surbrillance de survol (un seul `QGraphicsPathItem`, contexte `Select` seulement).
- **Suppr universel et aide** : désormais **entièrement L5** (décision du Lead sur le chevauchement avec L1 : L1 ne touche ni au dialogue d'aide ni à Suppr). T4 retourne les `QEXPECT_FAIL` de `test_ui_characterization.cpp:362` et `:378`.

## 3. Ligne d'indications et retours de curseur

- Widget : `QLabel* hintsLabel_` (size policy horizontale `Ignored`, `setMinimumWidth(0)` : un long texte ne force jamais la largeur de la fenêtre) en `statusBar()->addPermanentWidget(hintsLabel_, 1)` **avant** `toolLabel_` (donc jamais masqué par `showMessage`). Texte élidé (`QFontMetrics::elidedText`) ; l'infobulle porte le texte complet ; `accessibleName` « Indications de geste ».
- Alimentation : `connect(view_, &CanvasView::modifiersChanged, ...)` et `setTool` → `hintsLabel_->setText(format(InteractionMap::hintsFor(ctx, mods)))`, format « Clic : sélectionner · Maj : ajouter · Ctrl : basculer · Alt : objet dessous · Molette : zoom · Clic molette : panoramique ». Aucun texte de geste écrit à la main dans `main_window.cpp`.
- Aide contextuelle = cette ligne ; les messages `showMessage` existants (rappel de geste à l'activation d'un outil, `:5514-5560`) restent, ils ne dupliquent pas la ligne permanente.
- Curseurs : §2.6. Préréglage : au changement, `InteractionMap::setPreset` puis rafraîchissement de la ligne ; aucun redémarrage.

## 4. Menu Aide (accessible et documenté) — propriétaire T3 (dialogues) puis T4 (câblage)

Fichiers neufs : `apps/desktop/help_dialogs.{hpp,cpp}` (aucune dépendance à `MainWindow`).

| Entrée (`objectName`) | Contenu | Détail |
|---|---|---|
| **Guide de prise en main** (`helpGettingStartedAct`) | `GettingStartedDialog` : `QDialog` **non modal** (`setModal(false)`, `WA_DeleteOnClose`), 6 étapes courtes, chacune avec un bouton qui déclenche la vraie `QAction` | 1 Ouvrir une image (Ctrl+O) → 2 Segmenter (Segmentation ▸ Segmenter) → 3 Vectoriser la région ou « Numérisation automatique » → 4 Générer le remplissage / choisir le type de points (clic droit) → 5 Analyser (F5) → 6 Exporter en DST. Le constructeur reçoit `std::vector<Step{QString title, QString body, QAction* action}>` construit par `MainWindow` à partir de ses `QAction` existantes (les libellés ne sont donc pas dupliqués ; action nulle ou désactivée = bouton grisé). |
| **Gestes souris et clavier** (`helpGesturesAct`, **F1**) | `GesturesDialog` : `QDialog` non modal, champ de recherche (`QLineEdit`, `QSortFilterProxyModel` sur un `QStandardItemModel`) + `QTableView` 3 colonnes (Contexte / Geste / Action) | Source 1 : `InteractionMap::allRows()`. Source 2 : tous les `QAction` de la fenêtre ayant un `shortcut()` non vide et un texte (mnémonique `&` retiré). **Aucune ligne écrite à la main.** Les `QShortcut` nus (F, Échap, Entrée, Retour arrière) sont couverts par les lignes `Key` de la table. Remplace et **supprime** le `QMessageBox` périmé (`:5069-5081`) : l'entrée « Raccourcis clavier » devient cet écran (pas de doublon). |
| **À propos** | inchangé (`:5083-5089`) | |

- F1 : `QAction::setShortcut(QKeySequence::HelpContents)` sur `helpGesturesAct` (vérifié : `F1` libre aujourd'hui dans `main_window.cpp`). Le test `windowShortcutsAreUnique` le garde.
- Accessibilité : `accessibleName` sur chaque dialogue, la table, le champ de recherche (« Rechercher un geste ou un raccourci ») et les boutons d'étape ; ordre de tabulation recherche → table → Fermer ; `QTableView` sélection par ligne, navigation flèches, `Échap` ferme ; `setTabChangesFocus(true)` ; pas de couleur seule (texte « Fusion / Proposition » absent de l'écran utilisateur, réservé à la doc interne).
- Documentation : nouvelle section `## Souris et clavier` dans `docs/source/user-guide.md` (placée après « Le canevas », `:30-50`), contenant un bloc encadré par `<!-- GESTURES:BEGIN -->` / `<!-- GESTURES:END -->`
  **généré** par `openstitch_gesture_table --markdown` (exécutable minimal QtCore, `apps/desktop/gesture_table_tool.cpp`, lié à `interaction_map.cpp` seul). La section « Menu Aide » (`:275-278`) et « Raccourcis » (`:280-299`) sont mises à jour (Aide : 3 entrées + F1 ; la table des raccourcis garde ses lignes d'actions, la table des gestes est la nouvelle).
  Régénération : `openstitch_gesture_table --markdown > docs/gestures.generated.md` puis recopier le bloc (commande documentée dans la section) ; en CI **seul le test de dérive** compte.
- Test de dérive (CTest `docs_gestures_in_sync`, `tests/check_gesture_docs.cmake`) : `execute_process(openstitch_gesture_table --markdown)`, lit `user-guide.md`, extrait le texte entre les marqueurs, `if(NOT block STREQUAL generated) message(FATAL_ERROR "...")`. Compare le contenu exact (pas un grep partiel), comme `check_no_raw_sequence_bypass.cmake`. Déclaré dans `tests/unit/desktop/CMakeLists.txt` (donc absent du job `linux-core` sans Qt).
- Après modification de `docs/source/`, régénérer le PDF (`docs/scripts/build-docs.ps1`, règle `CLAUDE.md`) ; `docs/ui-redesign-specification.md:77` (« Aide : Guide de démarrage · Raccourcis clavier · À propos ») est mis à jour dans la même livraison.

## 5. Tests (QTest headless ; pas de `sleep` ; noms ASCII)

### 5.1 Logique pure — `tests/unit/desktop/test_interaction_map.cpp` (`QTEST_APPLESS_MAIN`, **sans QApplication**, QtCore seulement)
- `tableHasNoDuplicateContextGesture` ; `tableIdsAreUnique` ; `specificRowsMayOnlyShadowGlobalWhenOverrideFlagged` ; `plannedRowsAreNeverResolvedNorListed`.
- `everyRowResolvesToItself_data/()` : **une ligne de données par id de la table** (tag = `G1`…`N4`) → `resolve(row.context, row.gesture) == row.intent` (fait d'une seule fonction « une ligne de test par ligne de table », §4 de l'audit).
- `globalGesturesResolveInEveryContext` (G3 en `DrawFreeform`, `DrawClicks`, `NodeEdit`) ; `modifiersAreMatchedExactly` (Maj+clic ≠ clic) ; `ctrlWinsOverShiftForSelectMode`.
- `hintsWithoutModifierAreFlaggedRowsOnly` ; `hintsWithShiftListShiftRows` ; `hintsDifferBetweenSelectAndDrawContexts`.
- `touchpadPresetHidesMiddleButtonRowsAndKeepsSpacePan` ; `presetRoundTripsThroughQSettings` (`QSettings::setPath` + organisation temporaires, comme `main_window.cpp:393`) ; `longPressDelayIsClampedAndConfigurable`.
- `rectSelectsWindowRequiresFullContainment` ; `rectSelectsCrossingAcceptsPartialOverlap` ; `describeFormatsModifiersInFrench`.

### 5.2 Canevas — `tests/unit/desktop/test_canvas_input.cpp` (QTest + `QApplication`, offscreen) — rebasé sur l'existant
Le fichier existe (400 lignes, aides `prepareView`, `sendWheel`, `drag`). **Déjà verts, doivent le rester après T2** (caractérisation) : `leftClickEmitsCanvasClickedAtSceneCoordinates`, `ctrlClickAndShiftClickBehaveLikePlainClick` (**devient** `ctrlClick… Toggle` : à réécrire par le testeur en T2/T4), `rightClickEmitsContextMenuNotCanvasClicked`, `wheelUpZoomsInAndWheelDownZoomsOutByTheSameStep`, `wheelZoomKeepsTheSceneAnchorUnderTheCursor`, `wheelZoomIsClampedToTheSupportedRange`, `wheelWithModifiersZoomsLikePlainWheel` (Maj/Alt+molette y zooment : **à adapter**, Ctrl seul conserve le zoom), `spaceKeyHasNoEffectOnLeftDrag` (à adapter : Espace pan), `arrowKeysRequestNudges…`, `ctrlAndAltDoNotChangeTheNudgeStep`, `boxDrawReportsShiftFromTheReleaseEvent`.
**Verts uniquement avec `ScrollHandDrag` (CanvasView isolé, sans `setBaseContext`)** : `leftDragInEmptyAreaPansTheViewAndStillEmitsOneClick`, `shiftDragInEmptyAreaStillPans` : inchangés (§2.1) ; leurs équivalents « modèle activé » sont de nouveaux tests (`selectionRectangleReplacesLeftDragPanInSelectContext`).
**`QEXPECT_FAIL` existants qui FLIPPENT (XPASS → à retirer par le testeur)** : `shiftWheelZoomsInsteadOfScrollingHorizontallyYet` (`:224`, G6), `pixelDeltaOnlyWheelIsIgnored` (`:239`, G8), `middleButtonDragDoesNotPanNorClick` (`:260`, G3), `spaceDragDuringBoxDrawToolDrawsInsteadOfPanning` (`:299`, G5 sous outil de dessin). Le premier et le troisième flippent **dès T2 isolé** (branches additives) ; les autres aussi.
**Nouveaux tests** (tag = id de ligne) : G1 `wheelZoomInKeepsAnchor` (zoom **avant** ancré, ≤ 1 px) ; G2 `ctrlShiftMiddleDragZoomsContinuously` ; G4 `middleDoubleClickFitsCanvasWithoutGhostPan` (séquence press/release/dblclick/release) ; G5 `spaceDragPansViewAndResetsOnFocusOut`, `spaceDoesNotClickFocusedButton`, `spaceIgnoredWhenSpinBoxHasFocus`, `spaceFilterRemovedWithView` ; G6/G7 `altWheelWithXOnlyAngleDeltaScrollsVertically` ; G8 `pixelDeltaWheelPansNotZooms` (**sauté sous Windows** : `QSKIP`) ; G9 `nativePinchZoomsAtCursor` ; G13 `ctrlWheelZoomsInBothPresets`, `fractionalAngleDeltaZoomsProportionally`, `hugeAngleDeltaIsCapped` ; P1 `panToolLeftDragPansWithNoDragMode` ;
S1-S8 `plainClickEmitsCanvasClickedMm`, `shiftClickEmitsSelectionClickedAdd`, `ctrlClickEmitsSelectionClickedToggle`, `ctrlShiftClickIsToggle`, `longPressEmitsSelectBelow` (`setLongPressMsForTesting(1)`, `QSignalSpy::wait(500)`, **et** variante lisant `QSettings` dans un `setPath` temporaire), `altClickEmitsSelectBelow`, `altReleaseIsSwallowedAfterAltClick`, `dragRightwardEmitsWindowRectangle`, `dragLeftwardEmitsCrossingRectangle`, `shiftDragRectangleIsAdd`, `ctrlDragRectangleIsToggle`, `clickIsDeferredToReleaseInSelectContext`, `dragDoesNotEmitCanvasClicked`, `shiftPressOnSelectedBodyTogglesInsteadOfDragging`, `panWorksWhileFreeformToolActive` / `…BezierToolActive`, `modifiersComeFromEventNotGlobalState`, `modifiersChangedEmittedOnlyOnChange`, `cursorFollowsModifierInSelectContext`.
**Helper unique pour `QNativeGestureEvent`** : `makeNativeGesture(type, localPos, value)` dans `tests/unit/desktop/native_gesture_helper.hpp`, seul endroit où le constructeur est appelé, avec `#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)` (constructeurs à signatures différentes entre 6.4 et 6.8 : sans/avec `QPointingDevice*`) ; doit compiler sur **Qt 6.4.2 et 6.8.3**.
**`test_ui_characterization.cpp` à retourner en T4** : `:362` (Suppr objet vectoriel) et `:378` (Suppr broderie). Les autres `QEXPECT_FAIL` (actions sans document, dock, barre contextuelle, mode rails) restent à L1.

### 5.3 Aide, câblage, commandes
`test_help_dialogs.cpp` : `gesturesDialogRowsMatchAllRowsExactly`, `gesturesDialogListsEveryActionShortcut`, `gesturesDialogSearchFiltersRows`, `gesturesDialogHasAccessibleNamesAndTabOrder`, `gettingStartedHasSixStepsAndDisabledActionDisablesButton`, `dialogsAreNonModal`.
`test_composite_command.cpp` (`tests/unit/commands`, Catch2, noms ASCII) : `composite applies in order and reverts in reverse order`, `composite is one undo step`, `composite empty is a no-op`, `composite is deterministic across two runs`, `composite name is reported`.
`test_main_window.cpp` (T4) : `helpMenuHasThreeEntriesAndF1OpensGestures`, `oldShortcutsMessageBoxIsGone`, `hintsLabelSurvivesShowMessage`, `selectRectangleSelectsVectorObjects` (fenêtre vs croisement), `shiftClickAddsAndCtrlClickToggles`, `togglingPrimaryPromotesPrevious`, `selectionInvariantsHoldAfterDeleteUndoRedo`, `regionSelectionClearsMultiSelection`, `multiSelectionDisablesSingleObjectActions`, `inspectorShowsNObjets`, `deleteRemovesWholeMultiSelectionInOneUndoStep`, `selectAtReplaceMatchesLegacyOnCanvasClicked`, `panToolStillPansAfterNoDrag`, `shiftDragMovesAlongDominantAxis`, `ctrlDragSkipsSnap`, `altDragDuplicatesAndMovesCopy`, `altBoxDrawGrowsFromCenter`, `navigationPresetPersistsAndUpdatesHints`. Garde structurelle CTest `check_selection_single_mutator` (§2.7).
`test_ui_invariants.cpp` : `windowShortcutsAreUnique` vert avec F1 ; docks hors périmètre (L1).

### 5.4 Dérive docs
CTest `docs_gestures_in_sync` (§4). Test négatif manuel : altérer un libellé de la table sans régénérer la doc doit échouer.

## 6. Découpage en tâches de codeur (ordre imposé) et propriété des fichiers

Contrainte : **un seul écrivain à la fois sur `main_window.cpp/.hpp`** ; L1 l'édite en parallèle ; **toutes les modifications de `main_window.*` de L5 sont dans T4**, après fusion de L1 (rebase). Ruling Lead : L1 ne touche plus ni l'aide ni Suppr.

| Tâche | Ordre | Fichiers (propriété exclusive) | Dépend de |
|---|---|---|---|
| **T1 — Table, `CompositeCommand`, docs** | 1 | `apps/desktop/interaction_map.{hpp,cpp}`, `apps/desktop/gesture_table_tool.cpp`, `apps/desktop/CMakeLists.txt`, `libs/commands/include/openstitch/commands/composite_command.hpp` + `src/composite_command.cpp` + `libs/commands/CMakeLists.txt`, `tests/unit/commands/test_composite_command.cpp` + son `CMakeLists.txt`, `tests/unit/desktop/test_interaction_map.cpp`, `tests/check_gesture_docs.cmake`, `tests/CMakeLists.txt` (garde), `tests/unit/desktop/CMakeLists.txt` (ajouts), `docs/source/user-guide.md`, `docs/ui-redesign-specification.md:77` | rien |
| **T2 — CanvasView (inerte tant que T4 absent)** | 2 (parallèle à T3) | `apps/desktop/canvas_view.{hpp,cpp}`, `apps/desktop/design_tokens.{hpp,cpp}` (token `canvasSelectionRect`), `tests/unit/desktop/test_canvas_input.cpp`, `tests/unit/desktop/native_gesture_helper.hpp`, `tests/unit/desktop/test_canvas_view.cpp` (ajouts) | T1 |
| **T3 — Dialogues d'aide** | 2 (parallèle à T2) | `apps/desktop/help_dialogs.{hpp,cpp}`, `tests/unit/desktop/test_help_dialogs.cpp` | T1 |
| **T4 — Câblage `MainWindow`, multi-sélection, modificateurs** | 3 (dernier ; **T2+T4 dans un même commit** ou T2 inerte) | `apps/desktop/main_window.{hpp,cpp}`, `apps/desktop/node_handle.hpp` (modificateurs M2-M4/N2), `tests/unit/desktop/test_main_window.cpp`, `test_ui_invariants.cpp`, `test_ui_characterization.cpp` (retourner `:362`, `:378`), `tests/unit/desktop/test_node_handle.cpp` (ajouts), `tests/check_selection_single_mutator.cmake` | T1, T2, T3, fusion de L1 |

### Critères d'acceptation (mesurables)
- **T1** : `test_interaction_map` vert (un tag par id `G1…N4`, `P1`, sans QApplication) ; `test_composite_command` vert (5 cas) ; `ctest -R docs_gestures_in_sync` vert et **rouge** si un libellé est altéré ; sortie du générateur identique sur deux exécutions ; aucun `<QtWidgets>` dans `interaction_map.*` ; `linux-core` (sans Qt) toujours vert avec la nouvelle commande ; `clang-format --dry-run --Werror` propre.
- **T2** : `test_canvas_view` (6 tests) et les tests « déjà verts » de §5.2 verts ; les 4 `QEXPECT_FAIL` listés flippent et sont retirés (0 XPASS restant) ; tous les nouveaux tests verts sur **Qt 6.4.2 (build/linux-qt)** ; `grep -n "QGuiApplication::keyboardModifiers\|enterEvent.*setFocus" apps/desktop/canvas_view.cpp` = 0 ; l'application construite avec T2 seul se comporte comme avant (ScrollHandDrag).
- **T3** : `test_help_dialogs` vert (le test de cardinalité échoue si une ligne de table manque).
- **T4** : suite desktop complète verte (`test_main_window` 85 + nouveaux, `test_ui_invariants`, `test_ui_characterization` avec 2 XFAIL en moins, `test_canvas_*`) ; `check_selection_single_mutator` vert ; `windowShortcutsAreUnique` sans régression (F1) ; plus de `QMessageBox` « Raccourcis clavier » ; PDF de doc régénéré ; CI Windows verte avant de déclarer le lot fini (G8/G9 **non** exigés sous Windows).

## 7. Risques

| Risque | Parade |
|---|---|
| ~21 sites d'écriture de sélection : un oubli laisse `multiSelection_` périmée | Mutateur unique + invariants + élagage dans `refreshImage` + garde CTest structurelle |
| Collision avec L1 sur `main_window.cpp` | T4 après L1 ; L1 ne touche ni l'aide ni Suppr (ruling Lead) |
| `NoDrag` casse l'outil Pan ou les tests existants | Ligne P1 + branche dédiée ; défaut `ScrollHandDrag` pour une vue isolée ; commit commun T2+T4 |
| Clic de sélection différé au relâchement | Limité au contexte `Select` ; Crop/Draw* inchangés ; tests dédiés |
| Filtre applicatif d'Espace (portée globale) | Conditions strictes (`cursorOverViewport_`, `focusWidget` non-saisie), retrait au destructeur, tests |
| Alt seul active la barre de menus (Windows) | `KeyRelease` d'Alt avalé après un geste Alt ; vérification manuelle Windows |
| Pavé tactile Windows indiscernable d'une molette | G13 Ctrl+molette dans les deux préréglages, zoom lissé/plafonné, pan = Espace+glisser, documenté ; G8/G9 non exigés sous Windows |
| `QNativeGestureEvent` : constructeur différent Qt 6.4 / 6.8 | Helper de test unique avec `#if QT_VERSION` |
| Appui long : valeur non confirmée | Constante, clé `QSettings` bornée, setter test-only |
| Surbrillance de survol coûteuse | Contexte `Select` seulement ; coalescence 16 ms |
| Dérive table / aide / doc | Table unique ; `allRows()` pour dialogue et générateur ; test de dérive exact |
| Touches 2/3 non implémentées | Lignes `planned`, jamais affichées avant livraison |
| Maj = ajout / verrou d'axe / contrainte | Levé par contexte (règle 3) ; test `hintsDifferBetweenSelectAndDrawContexts` |

## Revision 2 (revue de conception : APPROVE WITH CHANGES)
Blocants : (1) `selectionExtra_` remplacé par `multiSelection_` + mutateur unique `setSelection`, 21 sites listés, invariants, actions mono-objet désactivées, « N objets », promotion du précédent (§2.7) ; (2) `CompositeCommand` ajoutée à T1, réserve « valider avec l'architecte » supprimée ; (3) ligne `P1` + branche Pan, `ScrollHandDrag` conservé pour une vue isolée (§2.1).
Majeurs : (4) S1/S12 fusionnées ; (5) règle 5 précisée (routées vs `Existing`) ; (6) lignes G14 (F) et G15 (flèches) ajoutées, G10 limitée aux contextes sans retour anticipé ; (7) G13 Ctrl+molette dans les deux préréglages, zoom lissé/plafonné, limite Windows documentée, G8/G9 non exigés sous Windows ; (8) M2-M4/N2/D4 attribués à T4 avec tests, token `canvasSelectionRect` dans T2 (`design_tokens.*`), modificateur+press sur corps sélectionné = sélection ; (9) setter de délai test-only et `QSettings` respectant `setPath` ; (10) helper unique `QNativeGestureEvent` gardé par `QT_VERSION`.
Mineurs : (11) test Alt+molette x-only, zoom avant, `NoAnchor` ; (12) garde du double-clic milieu ; (13) filtre applicatif d'Espace à la place de `enterEvent` ; (14) `interaction_map` = Core+Gui sans Widgets ; (15) aide et Suppr entièrement à L5, T2 inerte ou commit commun avec T4 ; (16) §5.2 rebasée sur les tests existants (tests flippants nommés), citations corrigées (`:5065`, `:5386-5430`) ; (17) 87 `showMessage` ; (18) `KeyRelease` d'Alt seul capté.
