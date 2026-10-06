# Plan : lot L5 — modèle d'interaction souris/clavier et menu Aide

Status: draft (architecte, à relire par le relecteur de conception)
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
  `selectedEmbroidery_` (`main_window.hpp:572-574`, 53 occurrences de `selectedObject_` dans `main_window.cpp`).
  Maj/Ctrl+clic et le rectangle multi-objets exigent donc un **ensemble** de sélection (§2.7) — c'est le vrai coût de L5.
- `onCanvasClicked` (`main_window.cpp:6691-6862`) porte toute la logique de sélection (objets vectoriels via
  `objectPainterPath(object).contains(posMm)`, satin via `satinEmbroideryAt`, régions via `segmentation::region_at`).
- Suppr : `delRegionAct->setShortcut(QKeySequence::Delete)` (`:611`) ne supprime que la région (`deleteSelectedRegion`, `:6864`).
  « Suppr universel » est **déjà au périmètre de L1** (audit §5) : L5 ne le réécrit pas, il l'étend à l'ensemble (T4).
- Aide : `buildHelpMenu()` (`main_window.cpp:5066-5090`) = un `QMessageBox` en dur périmé + « À propos ». Touche **F** =
  `QShortcut` (`:5068`), Échap/Entrée/Retour/Retour arrière = `QShortcut` (`:5393-5440`) : sans `QAction`, donc invisibles
  d'une génération « depuis les actions » → ils entrent par la table (§1).
- Barre d'état : `toolLabel_`, `cursorLabel_` en `addPermanentWidget` (`:407-410`) ; ~60 `showMessage` sans délai partout.
  Un widget ajouté par `addWidget` serait **masqué** pendant tout `showMessage` : la ligne d'indications doit être un widget **permanent**.
- `tests/unit/desktop/CMakeLists.txt` référence déjà `test_canvas_input` et `test_ui_characterization` ; au moment de
  l'étude, `test_canvas_input.cpp` n'est qu'un **squelette de 4 lignes non suivi** (le testeur L0 l'écrit en parallèle) :
  il ne contient aucun `QEXPECT_FAIL` à retourner pour l'instant (cf. §5, liste « à retourner » à appliquer quand il existera).
- Qt de dev = 6.4.2 (Linux), CI = 6.8.3 : `QStyleHints::mousePressAndHoldInterval()` (6.5+) est **indisponible** → délai propre (§1.4).
- Qt-light : `interaction_map` ne doit inclure que QtCore (`Qt::` enums, `QString`, `QList`, `QSettings`), jamais Widgets.

## 1. Modèle de données : `InteractionMap` (`apps/desktop/interaction_map.{hpp,cpp}`)

### 1.1 Types (namespace `openstitch::desktop`)

```cpp
enum class Context : uint8_t {
  Global,      // repli : s'applique si le contexte actif n'a pas de ligne pour ce geste
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
  SelectReplace, SelectAdd, SelectToggle, SelectBelow, SelectRectangle, Deselect, EnterEdit, HoverHighlight,
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
`QSettings navigation/longPressMs`, bornée 300-1000. Annulé si le curseur s'éloigne de plus de `QApplication::startDragDistance()`
ou si le bouton est relâché avant l'échéance. Implémenté par un `QTimer` à tir unique dans `CanvasView` (pas `mousePressAndHoldInterval`, cf. §0).

### 1.5 Règles de conflit (testées, §5)
1. Clé `(context, gesture)` **unique** dans la table (hors filtre de preset ; dans un preset donné aussi).
2. Une ligne d'un contexte spécifique ne peut masquer une ligne `Global` de même geste que si `overridesGlobal` (ex. Suppr en `NodeEdit`).
3. La correspondance est **exacte** sur les modificateurs : Maj+clic ≠ clic. Les conflits « Maj = ajout (rectangle) / verrou d'axe (déplacement) /
   contrainte (dessin) » sont levés **par contexte** (`Select` / `Move` / `Draw*`), jamais par priorité implicite.
4. Chaque `id` unique ; chaque ligne `routed` a une `Intent` traitée par `CanvasView` (test d'exhaustivité du `switch`, `-Wswitch` en erreur).
5. Un geste `Global` ne doit pas coïncider avec un `QAction`/`QShortcut` de la fenêtre (réutilise `windowShortcutsAreUnique`, `test_ui_invariants.cpp:76`).

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
| G10 | Global | Clic droit | ContextMenu | E | non (existe : `contextMenuEvent`, `:225`) |
| G11 | Global | Suppr | DeleteSelection | E (régions seules → L1/T4) | non |
| G12 | Global | Échap | CancelTool | E | non |
| S1 | Select | Clic | SelectReplace | E | oui |
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
| S12 | Select | Clic dans le vide | Deselect | E | oui |
| M1 | Move | Glisser un objet | MoveObject | E | non (`VectorObjectBodyItem`) |
| M2 | Move | Maj + glisser | AxisLock | P | non → T3 |
| M3 | Move | Ctrl + glisser | SuspendSnap | P | non → T3 |
| M4 | Move | Alt + glisser | DuplicateOnMove | P | non → T3 |
| D1 | DrawClicks | Clic | DrawPoint | E | non |
| D2 | DrawClicks | Double-clic ou Entrée | FinishDraw | E | non |
| D3 | DrawBox | Maj | ConstrainShape (cercle/carré) | E (ellipse) ; angle 15° P | non |
| D4 | DrawBox | Alt | DrawFromCenter | P | non → T3/T4 |
| D5 | DrawClicks | Ctrl (tenu) | SuspendSnap | P | non |
| D6 | DrawClicks | Retour arrière | RemoveLastPoint | E | non |
| D7 | DrawClicks | Échap | CancelTool | E | non |
| N1 | NodeEdit | Glisser un nœud | MoveNode | E | non |
| N2 | NodeEdit | Maj / Ctrl + glisser | AxisLock / SuspendSnap | P | non → T3 |
| N3 | NodeEdit | Double-clic sur un segment | InsertNode | P | non (hors L5 si l'insertion n'existe pas : ligne `planned`) |
| N4 | NodeEdit | Suppr | DeleteNodes (`overridesGlobal`) | E (menu « Supprimer le nœud », `:2354`) | non |

Les lignes `Draw*` non routées servent l'aide et les hints ; leur exécution reste celle de `MainWindow`. Seules les lignes `routed` flippent des tests de comportement.
Variantes par contexte : chaque geste `Global` marqué ci-dessus est valable dans **tous** les contextes (donc panoramique possible pendant un outil de dessin, exigence de l'audit).

### 1.7 Préréglage « Touchpad / sans clic molette » (altère la table par masque, pas par seconde table)
- Masque `presets` : G2, G3, G4 = OpenStitch seul (pas de bouton milieu). G5 (Espace+glisser) devient la ligne `hint` principale du panoramique.
- Ajoutées en Touchpad seul : « Ctrl + molette » → ZoomAtCursor (`G1t`) ; Alt+clic gauche inchangé.
- G1 : en OpenStitch, une molette sans `pixelDelta` zoome ; en Touchpad, `pixelDelta` non nul = **pan** (G8), `angleDelta` seul + Ctrl = zoom.
- Persistance : `QSettings(QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"))` (même couple que `app_theme.cpp:12`), clé `navigation/preset` = `openstitch|touchpad`.
  `setPreset()` n'écrit pas ; `savePreset()` écrit. UI : sous-menu **Affichage ▸ Navigation** (`QActionGroup` exclusif, 2 choix), `objectName` `navPresetOpenStitch|Touchpad` (T4).
  Pas de préférences générales avant L6.

## 2. Changements de `CanvasView` (`canvas_view.{hpp,cpp}`) — propriétaire T2

Principe : `CanvasView` traduit les évènements Qt en `Gesture` (modificateurs lus sur `QMouseEvent/QWheelEvent/QKeyEvent`),
appelle `InteractionMap::resolve(currentContext(), g)` et exécute les intents `routed`. Aucune mutation du `Project` : tout ce qui touche à la
sélection sort en **signaux**, `MainWindow` décide (via les chemins/commandes existants).

### 2.1 Mode de glisser et contexte
- Constructeur `:37` : `setDragMode(NoDrag)`. `updateDragMode()` (`:93`) : branche `else` → `NoDrag` (au lieu de `ScrollHandDrag`) ; `RubberBandDrag` conservé
  pour `cropMode_/boxDrawMode_` (leur `rubberBandChanged` et `lastRubberBandMm_` ne changent pas : non-régression).
- Nouveau membre `Context baseContext_{Select}` + `void setBaseContext(Context)` (Select/Pan/NodeEdit/StitchEdit, posé par `MainWindow::setTool` et les modes d'édition).
  `currentContext()` privé : dérive Crop/DrawBox/DrawClicks/DrawBezier/DrawFreeform des six booléens existants, sinon `baseContext_`.
  Sans appel de `setBaseContext`, le défaut `Select` + sélection au rectangle **désactivée** (`setSelectionRectangleEnabled(bool)`, défaut false) conserve le comportement
  des tests existants de `CanvasView` isolé (`test_canvas_view.cpp:72`).

### 2.2 Panoramique (G3, G5, G8) — disponible dans tout contexte
- `mousePressEvent` (`:194`) : **avant** les branches freeform/bézier, si `button==Middle`, ou `button==Left && spaceHeld_`, ou résolution = `PanView` →
  `panning_=true`, mémorise la position du viewport, `applyModeCursor(Qt::ClosedHandCursor)`, `event->accept()`, **ne pas** appeler la base ni émettre `canvasClickedMm`.
- `mouseMoveEvent` (`:234`) : si `panning_`, `hScrollBar()->setValue(h - dx)` / `vScrollBar()->setValue(v - dy)` (déclenche `scrollContentsBy` → `viewChanged`, `:307`).
  Le signal `cursorMovedMm` reste émis. `mouseReleaseEvent` (`:247`) : fin du pan, restaure le curseur du contexte.
- Un pan en cours n'émet ni `freeformPointMm` ni `bezierPointDraggingMm` (test dédié : pan pendant `setFreeformDrawMode(true)`).

### 2.3 Espace et focus (G5)
- `keyPressEvent` (`:275`) : `Key_Space` et `!isAutoRepeat()` → `spaceHeld_=true`, curseur main ouverte, `event->accept(); return;` (sans appeler la base : pas de « clic » sur un bouton, `QGraphicsView` ne le transmet pas non plus).
  `keyReleaseEvent` (nouveau) : relâche. Remise à zéro dans `focusOutEvent` (nouveau), `hideEvent`, et `changeEvent(ActivationChange)` si la fenêtre devient inactive.
- Focus : `enterEvent` (nouveau) donne le focus au canevas (`setFocus(Qt::MouseFocusReason)`) **sauf** si `QApplication::focusWidget()` est un champ de saisie
  (`QLineEdit`, `QAbstractSpinBox`, `QTextEdit`, `QComboBox` éditable) : on ne vole jamais la frappe de l'inspecteur. Un bouton de barre d'outils (`Qt::TabFocus` seulement) perd donc le focus à l'entrée sur le canevas et Espace ne le « clique » pas.
  `StrongFocus` existe déjà (`:44`).

### 2.4 Molette, trackpad, gestes natifs (G1, G6-G9)
- Remplacer `applyZoom(factor, anchorUnderMouse)` par `zoomAt(double factor, QPointF viewportPos)` : calcule `scenePos = mapToScene(viewportPos)`, `scale(f,f)`, puis
  recentre pour que `scenePos` revienne sous `viewportPos` (ajustement des scrollbars). `zoomIn/zoomOut` (`:158-164`) gardent l'ancrage centre. Bornes `kMin/kMaxPxPerMm` inchangées.
- `wheelEvent` (`:186`) : `delta = pixelDelta non nul ? pixelDelta : angleDelta` ; si `angleDelta().y()==0` utiliser `angleDelta().x()` (Qt échange x/y avec Alt sur certaines plates-formes).
  Résolution : `Shift` → ScrollHorizontal, `Alt` → ScrollVertical, `pixelDelta` non nul sans Ctrl → PanView (G8), sinon ZoomAtCursor sur `event->position()`.
  Préréglage Touchpad : Ctrl+molette = zoom. `event->accept()` toujours.
- `bool viewportEvent(QEvent*) override` (nouveau) : `QEvent::NativeGesture` — `ZoomNativeGesture` → `zoomAt(1.0 + value(), position)`, `PanNativeGesture` → pan par `delta`,
  `SmartZoomNativeGesture` → `fitCanvas()`. Ne pas utiliser `QPinchGesture` (ne pas appeler `grabGesture`).
- Zoom continu G2 : glisser bouton milieu avec Ctrl+Maj, `factor = exp(-dy * 0.01)` ancré au point d'appui.
- Double-clic milieu G4 : `mouseDoubleClickEvent` (`:218`) → `fitCanvas()`.

### 2.5 Sélection (S1-S8, S12) — activée par `setSelectionRectangleEnabled(true)` (posé par `setTool(Select)`)
- Nouveau `enum class SelectMode { Replace, Add, Toggle };` (dans `interaction_map.hpp`). Mapping : aucun modificateur → Replace ; **Maj → Add** ; **Ctrl → Toggle** ;
  si Maj+Ctrl, Ctrl l'emporte (documenté, testé).
- `mousePressEvent` : bouton gauche sur le vide ou un item non interactif (même garde `onInteractiveItem`, `:209-211`) → mémorise `pressViewportPos_`, `pressMods_`, **ne pas émettre** immédiatement
  (le clic est différé au relâchement pour ne pas désélectionner au début d'un glisser). Démarre le timer d'appui long (§1.4).
- `mouseMoveEvent` : si distance > `startDragDistance()` → annule le timer d'appui long, passe en rectangle ; un `QRubberBand` enfant du viewport (créé paresseusement, détruit au relâchement ; tokens `canvasSelection*` — aucune couleur en dur) montre le cadre.
  Le sens est lu **sur l'évènement de relâchement** : `crossing = releasePos.x() < pressPos.x()`.
- `mouseReleaseEvent` : rectangle → `emit selectionRectangleMm(QRectF sceneMm, SelectMode, bool crossing)` ; simple clic → `emit canvasClickedMm(pos)` si mode Replace (**compatibilité des connexions existantes**) sinon `emit selectionClickedMm(pos, mode)`.
- Appui long (timer) ou Alt+clic : `emit selectBelowRequested(QPointF sceneMm, QPoint globalPos, SelectMode)`. Un appui long consommé n'émet ni clic ni rectangle au relâchement.
- Survol (S11) : `mouseMoveEvent` émet déjà `cursorMovedMm` ; `MainWindow` y calcule la surbrillance (§2.7), pas de nouveau signal. Si le coût s'avère visible sur gros projets : `QTimer` de coalescence 16 ms dans `MainWindow`.
- Les contextes `Crop/Draw*` ne changent pas : en particulier `canvasClickedMm` y reste émis à l'appui (le polygone et la colonne satin comptent sur ce timing, `main_window.cpp:6700-6749`).

### 2.6 Curseurs et modificateurs
- `updateModifierCursor(Qt::KeyboardModifiers)` : `Select` + Maj → curseur « + » ; + Ctrl → « ± » ; `Move` + Alt → « copie » ; `spaceHeld_` → main ouverte ; pan actif → main fermée. Les « + / ± / copie » sont dessinés par code (`QPixmap` 24×24 recoloré par token, créé une fois), via `applyModeCursor` (`:71`, vue **et** viewport).
- Les modificateurs sont lus dans `mouseMoveEvent`, `keyPressEvent`, `keyReleaseEvent`, `enterEvent` (`event->modifiers()`), **jamais** `QGuiApplication::keyboardModifiers()`. À vérifier par test : `QKeyEvent` d'un appui sur Maj porte `ShiftModifier` (Qt 6.4 et 6.8).
- Nouveau signal `modifiersChanged(Qt::KeyboardModifiers)` émis seulement quand la valeur observée change (alimente la ligne d'indications).

### 2.7 Signaux nouveaux et ce que `MainWindow` doit connecter (T4)
```cpp
void selectionRectangleMm(QRectF rectMm, SelectMode mode, bool crossing);
void selectionClickedMm(QPointF posMm, SelectMode mode);            // seulement mode != Replace
void selectBelowRequested(QPointF posMm, QPoint globalPos, SelectMode mode);
void modifiersChanged(Qt::KeyboardModifiers);
```
Dans `MainWindow` (T4) :
- Ensemble de sélection minimal : `std::vector<ObjectId> selectionExtra_` (objets vectoriels **en plus** de `selectedObject_`, qui reste l'objet principal pour l'inspecteur et toutes les actions existantes). Le surbrillance de sélection l'affiche ; Suppr, visibilité et déplacement par flèches opèrent sur principal + extra via des commandes **existantes** appelées en boucle dans un `UndoStack` composite (vérifier s'il existe une macro-commande ; sinon une `commands::CompositeCommand` est un petit ajout hors `main_window` à valider avec l'architecte avant T4). Régions et broderie restent mono-sélection (Maj/Ctrl y valent Replace, documenté).
- `selectAt(QPointF, SelectMode)` : extraction du corps vectoriel/satin/région de `onCanvasClicked` (`:6750-6862`) ; `onCanvasClicked(pos)` = `selectAt(pos, Replace)` (comportement identique, test de non-régression).
- `onSelectionRectangle(rect, mode, crossing)` : pour chaque `project_.vector_objects` visible, `InteractionMap::rectSelects(rect, objectPainterPath(o), crossing)` ; Replace remplace, Add ajoute, Toggle bascule.
- `onSelectBelow(pos, globalPos, mode)` : liste des objets dont le tracé contient `pos` (vectoriels, puis satin, puis régions), `QMenu` « Sélectionner dessous » (une entrée par objet, nom + type), choix → même chemin que `selectAt`. Menu vide → message d'état.
- `setTool` (`:5442`) : `view_->setBaseContext(...)`, `view_->setSelectionRectangleEnabled(tool == Tool::Select)`, rafraîchit la ligne d'indications ; les modes d'édition de nœuds/points (`satinEditModeAct_`, `stitchEditModeAct_`, `railEditModeAct_`) appellent `setBaseContext(NodeEdit|StitchEdit)`.
- Le lambda `cursorMovedMm` (`:419-440`) calcule la surbrillance de survol (item `QGraphicsPathItem` unique, créé à la demande, supprimé avec la scène) uniquement dans le contexte `Select`.

## 3. Ligne d'indications et retours de curseur

- Widget : `QLabel* hintsLabel_` en `statusBar()->addPermanentWidget(hintsLabel_, 1)` **avant** `toolLabel_` (donc jamais masqué par `showMessage`). Texte élidé (`QFontMetrics::elidedText`) ; l'infobulle porte le texte complet ; `accessibleName` « Indications de geste ».
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

### 5.2 Canevas — `tests/unit/desktop/test_canvas_input.cpp` (QTest + `QApplication`, offscreen ; propriétaire testeur, rédigé **avant** T2 pour les tests de caractérisation, complété ensuite)
Un test de comportement par ligne `routed` (tag = id), via `QTest::mouse*` (bouton + modificateurs) et `QWheelEvent`/`QNativeGestureEvent` injectés par `QApplication::sendEvent(view.viewport(), ...)` :
`middleDragPansView` (G3, compare `horizontalScrollBar()->value()`), `spaceDragPansViewAndResetsOnFocusOut` (G5), `spaceDoesNotClickFocusedButton` (fenêtre avec `QPushButton` focusé puis entrée sur le canevas),
`wheelZoomKeepsSceneAnchorUnderCursor` (G1 : `mapToScene(pos)` avant/après identique à 1 px près), `shiftWheelScrollsHorizontally`/`altWheelScrollsVertically` (G6/G7), `pixelDeltaWheelPansNotZooms` (G8), `nativePinchZoomsAtCursor` (G9), `middleDoubleClickFitsCanvas` (G4), `ctrlShiftMiddleDragZoomsContinuously` (G2),
`plainClickEmitsCanvasClickedMm` (S1/S12, non-régression de `test_canvas_view.cpp:72`), `shiftClickEmitsSelectionClickedAdd` (S2), `ctrlClickEmitsSelectionClickedToggle` (S3), `ctrlShiftClickIsToggle`,
`longPressEmitsSelectBelow` (S4, `QSettings` délai = 1 ms, `QSignalSpy::wait(500)`), `altClickEmitsSelectBelow` (S5), `dragRightwardEmitsWindowRectangle` / `dragLeftwardEmitsCrossingRectangle` (S6), `shiftDragRectangleIsAdd` (S7), `ctrlDragRectangleIsToggle` (S8), `clickIsDeferredToReleaseInSelectContext` et `dragDoesNotEmitCanvasClicked`,
`panWorksWhileFreeformToolActive` / `panWorksWhileBezierToolActive` (pas de `freeformPointMm`), `modifiersComeFromEventNotGlobalState` (injection d'un `QMouseEvent` avec `ShiftModifier` sans toucher à l'état clavier global), `modifiersChangedEmittedOnlyOnChange`, `cursorFollowsModifierInSelectContext`.
**Tests de caractérisation déjà attendus (avant T2)** : clic gauche → `canvasClickedMm` ; flèches → `nudgeRequestedMm` (Maj = 1 mm) ; `boxDrawnMm` porte Maj ; molette actuelle zoome. Ils **restent verts** après T2.
**À retourner (`QEXPECT_FAIL(Continue)` → retiré par le testeur après T2)** : tout test de la liste ci-dessus dont le comportement n'existe pas encore (G2-G9, S2-S8, panoramique sous outil de dessin, Espace).

### 5.3 Aide — `tests/unit/desktop/test_help_dialogs.cpp`
`gesturesDialogRowsMatchAllRowsExactly` (même cardinalité et mêmes textes que `allRows()`), `gesturesDialogListsEveryActionShortcut` (parcourt les `QAction` d'une `MainWindow`), `gesturesDialogSearchFiltersRows`, `gesturesDialogHasAccessibleNamesAndTabOrder`, `gettingStartedHasSixStepsAndDisabledActionDisablesButton`, `dialogsAreNonModal`.
Dans `test_main_window.cpp` (T4) : `helpMenuHasThreeEntriesAndF1OpensGestures`, `oldShortcutsMessageBoxIsGone`, `hintsLabelSurvivesShowMessage`, `selectRectangleSelectsVectorObjects` (fenêtre vs croisement sur deux formes), `shiftClickAddsToSelectionAndCtrlClickToggles`, `selectAtReplaceMatchesLegacyOnCanvasClicked`, `navigationPresetPersistsAndUpdatesHints`.
`test_ui_invariants.cpp` : `windowShortcutsAreUnique` doit rester vert avec F1 ; `everyDockHasAnObjectNameAndIsReopenableFromAMenu` hors périmètre (L1).

### 5.4 Dérive docs
CTest `docs_gestures_in_sync` (§4). Test négatif manuel documenté : modifier un libellé de la table sans régénérer la doc doit faire échouer le test.

## 6. Découpage en tâches de codeur (ordre imposé) et propriété des fichiers

Contrainte : **un seul écrivain à la fois sur `main_window.cpp/.hpp`** ; L1 (corrections de câblage) l'édite en parallèle ; **toutes les modifications de `main_window.*` de L5 sont dans T4**, lancée après la fusion de L1 (rebase sur son commit).

| Tâche | Ordre | Fichiers (propriété exclusive) | Dépend de |
|---|---|---|---|
| **T1 — Table et docs** | 1 | `apps/desktop/interaction_map.{hpp,cpp}`, `apps/desktop/gesture_table_tool.cpp`, `apps/desktop/CMakeLists.txt` (ajouts), `tests/unit/desktop/test_interaction_map.cpp`, `tests/unit/desktop/CMakeLists.txt` (ajouts T1), `tests/check_gesture_docs.cmake`, `docs/source/user-guide.md`, `docs/ui-redesign-specification.md:77` | rien |
| **T2 — CanvasView** | 2 (parallèle à T3) | `apps/desktop/canvas_view.{hpp,cpp}`, `tests/unit/desktop/test_canvas_input.cpp`, `tests/unit/desktop/test_canvas_view.cpp` (ajouts seulement) | T1 |
| **T3 — Dialogues d'aide + modificateurs de déplacement** | 2 (parallèle à T2) | `apps/desktop/help_dialogs.{hpp,cpp}`, `apps/desktop/node_handle.hpp` (callbacks `onReleased/onMoved` avec `Qt::KeyboardModifiers` ; Maj = verrou d'axe via `mouseMoveEvent`, Ctrl = sans accroche, Alt = duplication : surcharge à signature compatible, anciens appelants inchangés), `tests/unit/desktop/test_help_dialogs.cpp`, `tests/unit/desktop/test_node_handle.cpp` (ajouts) | T1 |
| **T4 — Câblage `MainWindow`** | 3 (dernier, seul écrivain de `main_window.*`) | `apps/desktop/main_window.{hpp,cpp}`, `tests/unit/desktop/test_main_window.cpp`, `tests/unit/desktop/test_ui_invariants.cpp` (retirer les `QEXPECT_FAIL` devenus faux, uniquement ceux liés à L5) | T1, T2, T3, **fusion de L1** |

### Critères d'acceptation (mesurables)
- **T1** : `test_interaction_map` vert (tous les tags `G1…N4` présents, sans QApplication) ; `ctest -R docs_gestures_in_sync` vert, et **rouge** si on altère un libellé de la table ; `openstitch_gesture_table --markdown` déterministe (deux exécutions identiques octet à octet) ; aucun `#include <QtWidgets>` dans `interaction_map.*` ; `clang-format --dry-run --Werror` propre.
- **T2** : `test_canvas_view` (6 tests existants) **inchangés et verts** ; `test_canvas_input` : tous les tests §5.2 verts, 0 `QEXPECT_FAIL` restant concernant ces comportements, 0 XPASS ; `wheelZoomKeepsSceneAnchorUnderCursor` ≤ 1 px d'écart ; `grep -n "QGuiApplication::keyboardModifiers" apps/desktop/canvas_view.cpp` = 0 résultat ; `grep -n "ScrollHandDrag" apps/desktop/canvas_view.cpp` = 0 résultat ; aucun nouvel accès à `document::Project` depuis `canvas_view.*`.
- **T3** : `test_help_dialogs` vert (le test de cardinalité échoue si une ligne de table manque dans la boîte) ; `test_node_handle` existant vert ; surcharges de `node_handle.hpp` compilent sans toucher `main_window.cpp` (anciens appelants).
- **T4** : suite desktop complète verte sur `build/linux-qt` (`test_main_window` 85 tests existants + nouveaux, `test_ui_invariants`, `test_canvas_*`) ; `windowShortcutsAreUnique` ne régresse pas (F1 libre ; le XPASS/XFAIL de la touche G reste l'affaire de L1) ; le `QMessageBox` « Raccourcis clavier » (`:5071`) a disparu (`grep -n '"Raccourcis clavier"' apps/desktop/main_window.cpp` ne trouve que le titre de l'entrée de menu/dialogue généré) ; ligne d'indications visible après un `showMessage` (test) ; PDF de doc régénéré ; CI Windows verte avant de déclarer le lot fini.

## 7. Risques

| Risque | Parade |
|---|---|
| Sélection mono-objet : Maj/Ctrl+clic exigent un ensemble ; 53 occurrences de `selectedObject_` | `selectionExtra_` additif, principal inchangé ; régions/broderie hors multi-sélection (documenté) ; si une macro-commande manque, l'architecte valide un `CompositeCommand` avant T4 |
| Collision avec L1 sur `main_window.cpp` et sur « Suppr universel »/« aide générée depuis les actions » | T4 après fusion de L1 ; L1 ne touche pas à l'aide (reprise ici) ; T4 n'étend que Suppr à `selectionExtra_` |
| Clic de sélection différé au relâchement (changement de timing) | Limité au contexte `Select` ; Crop/Draw* inchangés ; tests `clickIsDeferredToReleaseInSelectContext` et non-régression de `test_canvas_view.cpp:72` |
| Espace : vol de focus, touche « collée » | Garde sur `focusWidget()` de saisie ; remise à zéro sur focus/hide/désactivation ; auto-répétition ignorée ; test dédié |
| Alt+clic : Windows peut activer la barre de menus au relâchement de Alt | `event->accept()` sur la séquence ; vérification manuelle Windows/CI (hors Qt offscreen) |
| `QKeyEvent` Maj/Ctrl : modificateurs de l'évènement vs état global selon plate-forme | Test d'injection ; lecture évènementielle seule |
| Appui long : valeur et ressenti non confirmés par Autodesk | Constante + clé `QSettings` bornée ; marqué « proposition » |
| Qt 6.4 (dev) ≠ 6.8 (CI) : `QNativeGestureEvent`, `mousePressAndHoldInterval` | Aucune API ≥ 6.5 ; gestes natifs testés par évènements injectés ; CI Windows arbitre |
| Surbrillance de survol coûteuse (`objectPainterPath` par objet à chaque déplacement) | Contexte `Select` seulement ; coalescence 16 ms si mesuré lent |
| Dérive table / aide / doc | Table unique ; `allRows()` pour dialogue et générateur ; test de dérive exact |
| Touches 2/3 (lasso, pinceau) non implémentées | Lignes `planned`, jamais affichées ni documentées avant livraison |
| Maj = ajout (sélection) vs verrou d'axe (déplacement) vs contrainte (dessin) | Levé par contexte ; règle de conflit 3 ; test `hintsDifferBetweenSelectAndDrawContexts` |
