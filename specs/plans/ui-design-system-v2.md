# Plan : lot L2 — Design system v2 (identité visuelle propre d'OpenStitch)

Status: revision 1 (architecte ; à relire par le relecteur de conception avant codage)
Référence : `docs/ui-audit-2026-10.md` §3.1 (décision du 2026-10-06 : identité propre, pas de rendu natif Windows ;
Qt « Fusion » = simple base neutre habillée par tokens + QSS) et §6 (L2 « à faire »), `docs/ui-redesign-specification.md` §19.
Public : relecteur de conception, 4 codeurs, testeur, Lead. Rien ici n'est du code de production.

## 0. État réel (vérifié par lecture/grep le 2026-10-06, branche `claude/zealous-sagan-fp5grf`)

- `Tokens` (`design_tokens.hpp:14-57`) : 6 surfaces/texte, accent+hover, selection(+Text), focus, 4 états, 12 champs canevas, métrique
  `space1..4` = 2/4/8/12 (`design_tokens.cpp:9-12`), `radiusSm/Md` = 3/5 (`:13-14`), `controlHeight` 22/28 (`:16`), `iconSize` 16.
  Aucun token de typographie, d'élévation, de mouvement, ni de variantes de bouton.
- `AppTheme` (`app_theme.cpp`) : `build_palette` (`:16`) et `build_stylesheet` (`:42-141`) dans un espace de noms anonyme, concaténation
  `%1.arg()` (~25 règles ; couvre QMenu/QToolBar/QDockWidget/QPushButton/champs/listes/QGroupBox/QScrollBar seulement). Appliqués par
  `app.setPalette` + `app.setStyleSheet` (`:155-156`, `:163-164`). Persistance `ui/theme`, `ui/density` (`load/save`).
- `main.cpp:10-18` : `QApplication app` puis `AppTheme::applyToApp` ; **aucun `QApplication::setStyle`** → sous Windows Qt 6.4/6.8
  rend en « windowsvista »/« windows11 » natif. **Les suites QTest n'appellent jamais `applyToApp`** (grep `AppTheme` dans
  `tests/unit/desktop` : 0 occurrence) : elles tournent sans QSS, style par défaut de la plateforme offscreen.
- Qt de dev = 6.4.2 (`pkg-config`), CI Windows = 6.8.3 : tout code doit compiler sur les deux.

### 0.1 Inventaire des visuels codés en dur (`apps/desktop`, hors `design_tokens.cpp`)

| Catégorie | Nb | Pires occurrences (fichier:ligne) |
|---|---|---|
| `setStyleSheet` hors `app_theme.cpp` | **5** | `empty_state_widget.cpp:16` (cadre, bordure, rayon), `import_dialog.cpp:71` (warning, via token : OK mais ad hoc), `main_window.cpp:3633` (pastille `rgb(%1,%2,%3)` + `palette(mid)`), `:4071` et `:4257` (`color:#8a5a00;`) |
| Hex `#rrggbb` | **2** | `main_window.cpp:4071`, `:4257` (même valeur ; 5,93:1 sur blanc — meilleure que le token `warning` actuel `B4791F`, 3,69:1) |
| `QColor(<nombres>)` | **13 littéraux** | `main_window.cpp` : `:1692, :2157, :2320` (aperçus polygone/Bézier/libre `80,120,200`), `:2013` (connecteurs), `:2001, :2209, :3093` (rail A `200,90,40`), `:3094` (rail B `40,130,200`), `:1943` (accroche `255,140,0`, 2,33:1 sur blanc), `:4192` (ligne de coupe `B03030`) ; `ui_icons.cpp:20` (`kInk` `5C626A`, **2,21:1 sur surface sombre `2B2E34`**, 2,46:1 sur fenêtre sombre), `:183` (pointillé rouge) ; `ai_segmentation_dialog.cpp:447` (masque `255,90,0`) |
| `Qt::white` | 1 | `canvas_view.cpp:1091` (papier). **Défaut réel** : en thème sombre `canvasGrid` = blanc alpha 28 et `canvasStitch` = `C8CAD8` (1,63:1) sont dessinés sur ce papier blanc → grille invisible ; fond clair `EBEBEE` quasi indiscernable du papier (1,2:1) |
| `QColor(var,var,var)` (donnée, pas identité) | 4 | `document_panel.cpp:19`, `main_window.cpp:6467, :6603` (pastilles de couleur de fil), `ai_segmentation_dialog.cpp:450` (gris calculé) : légitimes, **non visés** par le garde |
| Polices | 9 sites | `main_window.cpp:5309` (`Consolas` en dur), `empty_state_widget.cpp:26-29` (`pointSizeF()+2`), `properties_panel.cpp:35-37` (gras), `help_dialogs.cpp:409-411`, `ruler.cpp:35-37` (`7.5` pt), `canvas_view.cpp:118-121` (`setPixelSize(12)`) |
| Tailles fixes | 1 `setFixedSize(24,24)` (`main_window.cpp:3632`) ; `setIconSize(18,18)` `:5520`, `(20,20)` `:5763` ; `resize()` dialogues : `main_window.cpp:375` (1100×800), `:5303`, `help_dialogs.cpp:137,355`, `ai_*_dialog.cpp` ; marges numériques `properties_panel.cpp:28-29` (8/6), `empty_state_widget.cpp:20-21` (28/24/10) |
| Widgets déjà tokenisés | — | `canvas_view.cpp:86,103,174,1111-1131`, `workflow_panel.cpp:112`, `help_dialogs.cpp:91,144,396,406`, `main_window.cpp` (≈25 sites `tokens().accent/canvas*`) : à **conserver** (les noms existants de `Tokens` ne sont PAS renommés) |

Surfaces de widgets à habiller (comptes `new X`/`X(` dans `apps/desktop/*.cpp`) : QCheckBox 22, QRadioButton 5, QSlider 3, QProgressBar 1,
QTabWidget 1, QTableWidget/View 11, QTreeWidget 4, QListWidget 6, QComboBox 15, spin 29, QGroupBox 2, QToolButton 2, QDockWidget 6, QToolBar 2.

## 1. Langage visuel (décisions par défaut ; veto possible, cf. §6.3)

**Forme.** Rayons `radiusSm 6 / radiusMd 10 / radiusLg 14 / radiusPill 999` (QSS borne le rayon à la demi-hauteur : à confirmer par snapshot).
Boutons : **un seul primaire plein** (`variant=primary`, fond accent, texte `onAccent`) ; secondaire = **tonal sans bordure** (`tonal`) ;
`ghost` (transparent, texte `text`, fond `tonal` au survol) pour barres d'outils/liens d'action ; `danger` (texte/fond `error`). Champs
(QLineEdit/spin/combo) = fond `surfaceRaised` + bordure 1 px `borderStrong` (≥ 3:1, seul indice de frontière d'un champ), focus = anneau 2 px.
`QGroupBox` **sans cadre** : titre petites capitales (`QFont::SmallCapitals` posé par `ui::styleGroupBox`, QSS ne sait pas) en semi-gras
`textSecondary` + filet `border` 1 px dessous. **Docks** séparés par le contraste `surface` (panneau) / `window` (fond) — pas de traits —
titre de dock sans bordure, `QSplitter::handle`/séparateur de dock 4 px `window`. **Barres de défilement** 8 px, poignée arrondie `borderStrong`
(`border` au repos si ≥ 3:1 impossible : poignée = non-texte, donc `borderStrong`), rails transparents, pas de flèches. **Élévation** :
QSS n'a pas d'ombre portée → niveau 0 `window`, 1 `surface`, 2 `surfaceRaised` + filet `border` 1 px (menus, infobulles, popups de combo) ;
l'ombre système des popups Windows est un bonus non garanti (non vérifiable hors écran), aucun `QGraphicsDropShadowEffect`.
**Anneau de focus** : bordure 2 px `focus` (bordure transparente de 2 px au repos pour ne pas bouger la mise en page) ; **le décalage
(« offset ») n'est pas réalisable en QSS** (pas d'`outline-offset`) → décision : pas de décalage, `focus` choisi ≥ 3:1 contre toutes les
surfaces ; un `QProxyStyle` serait contourné par `QStyleSheetStyle` pour les widgets habillés. **Mouvement** : aucun par défaut ; tokens
`motionShortMs = 120` (0 si `ui/reduceMotion` ou variable `OPENSTITCH_REDUCE_MOTION`), consommés plus tard par L3 (fondu du bandeau) ; ce lot n'anime rien.

### 1.1 Couleurs (valeurs finales, vérifiées par un script de calcul WCAG (même algorithme que §1.3, reproduit par `test_design_tokens`) — tableau §1.4 ; noms existants conservés, nouveaux marqués ★)

| Token | Clair | Sombre | Rôle |
|---|---|---|---|
| `window` | `#EBEDF1` | `#1B1D21` | fond de fenêtre/entre docks |
| `surface` | `#F5F6F8` | `#23262B` | panneaux, docks, barres |
| `surfaceRaised` | `#FFFFFF` | `#2C3036` | champs, menus, infobulles, cartes |
| `surfaceSunken` ★ | `#E2E5EA` | `#17181C` | puits : listes/tables, rail de curseur, zone de texte mono |
| `border` | `#D3D7DD` | `#363B43` | filets décoratifs (exemptés de contraste) |
| `borderStrong` ★ | `#767D86` | `#7F8794` | bordure de champ, case, poignée (≥ 3:1) |
| `text` / `textSecondary` | `#1B1E23` / `#555C67` | `#E8EAEE` / `#A4ABB5` | texte, aide/légende |
| `textDisabled` ★ | `#7A808A` | `#7C838D` | désactivé (règle maison ≥ 3:1, lisible) |
| `accent` / `Hover` ★ / `Pressed` ★ | `#B04E3C` / `#9A4130` / `#843727` | `#E0765F` / `#E88B77` / `#CF664F` | primaire, case cochée, sélection forte (rouge « fil », conservé) |
| `onAccent` ★ | `#FFFFFF` | `#1C100D` | texte/icône sur `accent*` |
| `tonal` ★ / `Hover` / `Pressed` | `#E4E7EC` / `#D9DDE4` / `#CDD2DA` | `#363B44` / `#40464F` / `#2F343C` | boutons secondaires |
| `selection` / `selectionText` | `#F2DAD3` / `#1B1E23` | `#4B302A` / `#F1F3F6` | **teinte** (plus de rouge plein) + liseré gauche 2 px `accent` (indice non-texte) |
| `focus` | `#1F5FC4` | `#6AA3F0` | anneau de focus |
| `icon` ★ | `#555C67` | `#A4ABB5` | icônes (remplace `kInk`) ; actives = `text`, désactivées = `textDisabled` |
| `success/warning/error/info` | `#2B6E3D #8A5A00 #B3261E #25598F` | `#6FBF86 #E0A93F #F07A6E #7FB0E8` | texte/icône d'état sur toute surface (≥ 4,5:1) |
| `canvasBackground` | `#D5D9DF` | `#15171A` | hors papier |
| `canvasPaper` ★ | `#FFFFFF` | `#E4E7EC` | **le tissu reste clair dans les deux thèmes** (les fils sombres restent visibles ; corrige le défaut §0.1) |
| `canvasGrid` / `canvasAxis` | `#1F2233`@40 / `#5A5F8C`@120 | identiques | alpha sombre sur papier clair (décoratifs) |
| `canvasStitch` | `#1F2233` | `#1F2233` | repli des points (était `C8CAD8` en sombre) |
| `canvasHoop` | `#C8383A` | `#C8383A` | cadre de broderie |
| `canvasJump` | `#B35C00` | `#B35C00` | sauts |
| `canvasNode` = `canvasHandle` | `#2463C6` | `#2463C6` | nœuds, poignées |
| `canvasRailA` ★ / `canvasRailB` ★ | `#C2531F` / `#1B7F8C` | identiques | rails satin (remplace `200,90,40` / `40,130,200`) |
| `canvasSnap` ★ | `#B8239A` | `#B8239A` | indicateur d'accroche (remplace `255,140,0`) |
| `canvasPreview` ★ | `#5560CC` | `#5560CC` | aperçus de tracé (remplace `80,120,200`) |
| `canvasCutLine` ★ | `#C0262D` | `#C0262D` | ligne de coupe satin (+ pointillé d'icône) |
| `canvasMask` ★ | `#E04A00` | `#E04A00` | surcouche du masque IA (`ai_segmentation_dialog.cpp:447`) |
| `canvasSelectionLine` / `…RectLine` | `#B04E3C` / `#2463C6` | identiques | sélection (trait) / cadre élastique |
| `canvasSelectionHalo` / `…RectHalo` | `#FFFFFF`@220 / `@200` | identiques | halo sous le trait (papier clair des deux côtés) |

Principe canevas : les surcouches sont dans la **bande de luminance 0,13–0,30** (≥ 3:1 à la fois contre le papier clair ET contre
`canvasBackground` sombre) ; les rails A/B n'ont que ≈ 1,02:1 d'écart de luminance (teinte seule) → l'indice non-couleur est
la **lettre A/B** déjà portée par les poignées (à vérifier par snapshot ; sinon trait B en pointillé).

### 1.2 Typographie, espacement, densité (nouveaux champs de `Tokens`)

- Pile `fontFamilies` = `"Segoe UI Variable Text", "Segoe UI", "Inter", "Noto Sans", "Helvetica Neue", "Arial"` appliquée par
  `QFont::setFamilies` (Qt ≥ 6.1) via `QApplication::setFont` ; mono = `"Cascadia Mono", "Consolas", "DejaVu Sans Mono", "Courier New"`.
- Tailles en **points** (suivent le DPI) : `fontSmall` 8, `fontBase` 9 (compact 8,5), `fontTitle` 11, `fontHeading` 13, `fontMono` 9 ;
  graisses `weightRegular 400`, `weightMedium 500`, `weightSemibold 600` (`QFont::Weight` Qt 6 ; QSS `font-weight:` numérique).
- Espacements : `space1..space7` = **2/4/8/12/16/24/32** (`space1..4` gardent leurs valeurs actuelles → aucun appelant cassé ; 5-7 nouveaux).
- Densité (`Density` inchangé) : `controlHeight` **32 / 26** (≥ 24 px, audit §3.6), `controlPadX` 12 / 8, `rowHeight` 28 / 24,
  `iconSize` 18 / 16, `toolIconSize` 20 / 18 (remplace `setIconSize(18)`/`(20)`), `scrollbarWidth` 8, `focusRingWidth` 2.
- Rôles de `QLabel` (`setProperty("role", …)`, QSS `QLabel[role="…"]`) : `title` (`fontTitle`, semi-gras), `heading`, `caption`
  (`fontSmall`, `textSecondary`), `mono`, `warning`/`error`/`success` (couleur d'état), `section` (petites capitales).

### 1.3 Règles de contraste (normatives) et algorithme

R1 texte (corps, étiquettes, secondaire, états) ≥ **4,5:1** sur chaque surface où il apparaît (`window/surface/surfaceRaised/surfaceSunken`,
`tonal*` pour `text`, `accent*` pour `onAccent`, `selection` pour `selectionText`) ; R2 non-texte (icône, `focus`, `accent`,
`borderStrong`, surcouches canevas) ≥ **3:1** contre leur fond ; R3 `textDisabled` ≥ 3:1 (règle maison, WCAG exempte le désactivé) ;
R4 exemptés : `border`, grille, axes, halos (décoratifs) ; R5 `canvasPaper`/`canvasBackground` ≥ 1,3:1. Un bouton tonal n'a pas de
bordure : son étiquette l'identifie (WCAG 1.4.11 « sauf si le libellé suffit »).
Algorithme (WCAG 2.x, **pur** : `design_tokens.hpp`, sans `QApplication`) : composer d'abord l'alpha de `fg` sur `bg` (`c = fg·a + bg·(1-a)`,
canaux 0-255), puis `c8/255 → lin = c ≤ 0,03928 ? c/12,92 : ((c+0,055)/1,055)^2,4` ; `L = 0,2126 R + 0,7152 G + 0,0722 B` ;
`ratio = (Lmax+0,05)/(Lmin+0,05)`. API : `double relative_luminance(QColor)`, `QColor composite(QColor fg, QColor bg)`,
`double contrast_ratio(QColor fg, QColor bg)`, et la **table unique** `std::span<const ContrastRule> contrast_rules()` où
`ContrastRule{ const char* name; QColor Tokens::*fg; QColor Tokens::*bg; double minRatio; }` — la même table alimente le test et le
`contrast.md` du dossier de snapshots.

### 1.4 Table de contraste (calcul hors dépôt, algorithme §1.3 ; 79 paires × 2 thèmes = 158 contrôles, 0 échec)

| Paire (fg sur bg) | Min | Clair | Sombre |
|---|---|---|---|
| `text` / `surface` | 4,5 | 15,45 | 12,60 |
| `textSecondary` / `surface` ; / `surfaceSunken` | 4,5 | 6,24 ; 5,34 | 6,56 ; 7,66 |
| `text` / `tonalHover` (pire cas tonal) | 4,5 | 12,27 | 7,90 |
| `onAccent` / `accent` | 4,5 | 5,25 | 6,13 |
| `selectionText` / `selection` | 4,5 | 12,53 | 10,76 |
| `success` / `surfaceSunken` (pire état) ; `error` ; `info` | 4,5 | 4,89 ; 5,18 ; 5,73 | 8,00 ; 6,52 ; 7,84 |
| `warning` / `surfaceRaised` | 4,5 | 5,93 | 6,26 |
| `icon` / `tonal` | 3 | 5,44 | 4,86 |
| `textDisabled` / `surface` | 3 | 3,68 | 3,97 |
| `accent` / `surface` (non-texte) ; `focus` / `surface` | 3 | 4,85 ; 5,56 | 5,01 ; 5,86 |
| `borderStrong` / `surfaceRaised` | 3 | 4,16 | 3,66 |
| `canvasRailB` / `canvasPaper` ; `canvasRailA` / `canvasBackground` | 3 | 4,71 ; 3,26 | 3,80 ; 3,89 |
| `canvasSnap` / `canvasBackground` ; `canvasPreview` / `canvasBackground` | 3 | 3,96 ; 3,77 | 3,20 ; 3,36 |
| `canvasPaper` / `canvasBackground` | 1,3 | 1,42 | 14,49 |
| *anciens* : `kInk` / surface sombre ; `#FF8C00` / blanc ; `B4791F` / blanc | — | — | 2,21 ; 2,33 ; 3,69 (échecs corrigés) |

Séparation panneau/fond (non normatif) : `surface`/`window` 1,08 (clair), 1,11 (sombre) — assez pour un dock « posé » sans trait ;
si le snapshot le juge trop faible, assombrir `window` clair à `#E6E9EE` (re-lancer le test).

## 2. Architecture

### 2.1 Fichiers (tous dans `apps/desktop/`, Qt confiné ici)

- `design_tokens.{hpp,cpp}` : `Tokens` étendu (§1), `light_tokens/dark_tokens`, contraste pur (§1.3). **Seule** source de littéraux de couleur.
- **`app_style.{hpp,cpp}`** (nouveau, **pur** : pas de `QApplication`) : `QPalette build_palette(const Tokens&)`, `QString build_stylesheet(const Tokens&)`,
  `QFont app_font(const Tokens&)`, `QStringList stylesheet_placeholders()` (pour test). Le QSS est un **gabarit unique** (littéral brut
  `R"qss(...)qss"`) à placeholders `@token@` (`@accent@`, `@radiusMd@`, `@space3@`…) substitués depuis **une** table `token_map(const Tokens&)`
  → un nom inconnu laisse un `@x@` détectable ; remplace la concaténation `%1.arg()`. Ni `.qrc` ni fichier externe.
- `style_assets.{hpp,cpp}` (nouveau) : glyphes que le QSS ne peut pas dessiner (coche, point radio, chevrons combo/spin/branche d'arbre),
  rendus par QPainter en `check.png`/`check@2x.png`… colorés par tokens, écrits dans `QStandardPaths::CacheLocation/ui-assets/<hash-des-couleurs>/`
  (surchargeable : `StyleAssets::setRootForTesting(QDir)`), référencés par `url(...)` en `/` ; si l'écriture échoue → QSS sans `image:`
  (boîte pleine = état par couleur seule : dégradé acceptable, loggué). Le choix de `@2x` auto par `QIcon` dans QSS est à valider (snapshot 2×).
- `ui_style.hpp` (nouveau, en-tête seul) : constantes `kPropVariant="variant"`, `kPropRole="role"`, `enum class ButtonVariant{Primary,Tonal,Ghost,Danger}`,
  `enum class LabelRole{Title,Heading,Caption,Mono,Warning,Error,Success,Section}`, aides `ui::setVariant(QAbstractButton*, ButtonVariant)`,
  `ui::setRole(QLabel*|QWidget*, LabelRole)` (posent la propriété puis `style()->unpolish/polish` ; no-op si inchangé), `ui::styleGroupBox`.
- `app_theme.{hpp,cpp}` : orchestre seulement (§2.2) ; garde `setMode/setDensity/mode()/density()/tokens()/changed()` **à l'identique**
  (compatibilité `main_window.cpp:856-870`) ; ajoute `ThemeChoice{System,Light,Dark}`, `themeChoice()`, `setThemeChoice()`.
- `ui_icons.{hpp,cpp}` : voir §2.4.

### 2.2 Style de base forcé et application du thème

`main.cpp`, **juste après** la construction de `QApplication` et avant `AppTheme::applyToApp` : `QApplication::setStyle(QStringLiteral("Fusion"))`
(retour `nullptr` ⇒ `qWarning` et on continue). **Ordre impératif** : `setStyle` réinitialise la palette (doc Qt) ⇒ jamais après
`setPalette`. `applyToApp(app)` = `load()` → `ensureFusion(app)` (idempotent : `if (app.style()->name().compare("fusion", CaseInsensitive))`)
→ `setFont(app_font)` → `setPalette(build_palette)` → `setStyleSheet(build_stylesheet)` → `emit changed()`. `reapply()` (changement de thème/densité) fait de
même **sans** `setStyle`, avec `setUpdatesEnabled(false)` sur les fenêtres de premier niveau pendant l'application, et un cache
`QHash<(mode,densité),QString>` du QSS (la chaîne n'est générée qu'une fois par combinaison). Une variable `QT_STYLE_OVERRIDE`
ou `-style` utilisateur est ignorée (identité imposée). **Tests** : les suites existantes restent sans thème (aucun changement, donc
aucune régression possible par QSS) ; la nouvelle suite `test_design_system` appelle `applyToApp` explicitement dans `initTestCase`
(avec `QSettings` isolés, comme `test_ui_invariants.cpp:95-102`) ; le snapshot l'appelle aussi (§4).

### 2.3 Couverture QSS complète (gabarit `app_style.cpp`) et propriétés

Sélecteurs obligatoires (chacun : repos, `:hover`, `:pressed`/`:checked`, `:disabled`, `:focus` quand pertinent) :
`QMainWindow, QDialog, QWidget#… fond window` ; `QToolTip` ; `QMenuBar(::item)` ; `QMenu(::item, ::separator, ::icon, ::right-arrow)` ;
`QToolBar(::separator, ::handle)` + `QToolButton` (variantes `ghost`, `:checked` = `tonalPressed`) ; `QStatusBar(::item)` ;
`QDockWidget(::title, ::close-button, ::float-button)` ; `QPushButton` + `[variant="primary|tonal|ghost|danger"]` (le défaut sans propriété = tonal ;
`QDialogButtonBox QPushButton:default` = primaire) ; `QLineEdit, QPlainTextEdit, QTextEdit, QAbstractSpinBox` (+ `::up-button/::down-button`) ;
`QComboBox(::drop-down, ::down-arrow)` + `QComboBox QAbstractItemView` (popup) ; `QCheckBox/QRadioButton(::indicator…)` ;
`QSlider(::groove, ::sub-page, ::handle)` ; `QProgressBar(::chunk)` ; `QTabWidget::pane`, `QTabBar::tab` ; `QSplitter::handle` ;
`QHeaderView::section` ; `QTableView/QTableWidget/QListView/QTreeView(::item, ::branch)` (+ `alternate-background-color` `surface`) ;
`QScrollBar`(2 orientations, ::handle, ::add-line/::sub-line à 0) ; `QGroupBox(::title)` ; `QLabel[role=…]` ; `QFrame[frameShape]`/`QFrame#…` séparateurs ;
`QRubberBand` non (peint par `SelectionBand`, `canvas_view.cpp:80-95`) ; `QGraphicsView` (fond = token, via `setBackgroundBrush` existant).
**Interdits dans le gabarit** (propriétés ignorées/ avertissement « Unknown property ») : `box-shadow`, `outline-offset`, `transition`, `font-variant`.
Le test §5 capture les `qWarning` de Qt pour le prouver. Nommage pour les tests/QSS : `objectName` existants conservés ; contrat
`variant` ∈ {primary,tonal,ghost,danger}, `role` ∈ {title,heading,caption,mono,warning,error,success,section} ; toute valeur inconnue = rendu par défaut (jamais d'erreur).

### 2.4 Suivi du thème système (compilable Qt 6.4 ET 6.8.3)

`ThemeChoice::System` (persisté `ui/theme = system|light|dark` ; **défaut conservé = light** pour les profils existants, `System` proposé dans le
menu). `AppTheme::resolvedMode()` : `Light/Dark` direct ; `System` →
```
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? Dark : Light;   // + connect(colorSchemeChanged) → reapply
#else
    return startupPrefersDark_ ? Dark : Light;   // luminance de QGuiApplication::palette() lue AVANT tout setStyle/setPalette (constructeur de applyToApp)
#endif
```
Sous 6.4 : pas de suivi en direct (relu au prochain démarrage) ; documenté dans l'aide du menu. Ne jamais appeler `QStyleHints::setColorScheme` (6.8).
`QPalette::Accent` (6.6+) posé sous `#if QT_VERSION >= QT_VERSION_CHECK(6,6,0)`.

### 2.5 Icônes peintes recolorées (pas la bibliothèque SVG : lot L4)

`ui_icons.cpp:16-20` : `kSize=32` fixe et `kInk` unique, pixmap figé à la construction. Remplacement de `make()` par un **`QIconEngine` maison**
(`TokenIconEngine`, privé à `ui_icons.cpp`) qui garde la fonction de dessin (viewbox 32×32, inchangée) et **repeint à chaque demande** :
`scaledPixmap(size, mode, state, scale)` rend `size×scale` px (HiDPI net, `setDevicePixelRatio(scale)`) avec la couleur du token **courant** :
`Normal/Off` → `icon`, `Active/Selected` → `text`, `Disabled` → `textDisabled` ; `clone()`, `paint()`, `pixmap()`, `key()="osicon"` implémentés.
Aucun appelant à changer (`icons::xxx()` renvoie toujours un `QIcon`) ; au changement de thème les widgets sont repolis par le QSS donc
re-demandent l'icône (le moteur n'a pas de cache). Le pointillé rouge `:183` → `canvasCutLine`. `AppTheme::changed` n'a pas besoin d'être écouté.
Limite connue : un `QIcon` copié dans un `QPixmap` figé par un appelant ne suit pas (aucun cas identifié ; un test l'attrape).

## 3. Visuels en dur à supprimer (fichier:ligne → remplacement) et garde CTest

| Où | Remplacement |
|---|---|
| `main_window.cpp:1692, :2157, :2320` `QColor(80,120,200)` | `tokens().canvasPreview` |
| `:2013` connecteurs `80,120,200` | `canvasPreview` |
| `:2001, :2209, :3093` `200,90,40` ; `:3094` `40,130,200` | `canvasRailA` ; `canvasRailB` |
| `:1943` `255,140,0` | `canvasSnap` |
| `:4192` `0xB03030` | `canvasCutLine` |
| `:4071, :4257` `setStyleSheet("color:#8a5a00;")` | `ui::setRole(warn, LabelRole::Warning)` |
| `:3632-3637` pastille `setFixedSize(24,24)` + `rgb(%1,%2,%3)`/`palette(mid)` | `QLabel` + `QPixmap` 24×24 (= `space6`) peint avec la couleur donnée et un filet `border` ; plus de `setStyleSheet` |
| `:5309-5311` `QFont("Consolas")` | `ui::setRole(editor, LabelRole::Mono)` (QSS `QPlainTextEdit[role="mono"]`) |
| `:5520`, `:5763` `setIconSize(18/20)` | `tokens().toolIconSize`, remis à jour sur `AppTheme::changed` |
| `:444-457` étiquettes d'état | `ui::setRole(..., Caption)` |
| `empty_state_widget.cpp:16-29` | `setObjectName("emptyState")` + QSS `QFrame#emptyState` ; titre `role=title` ; 1 bouton `primary` + 2 `tonal` ; marges `space6/space5` |
| `import_dialog.cpp:71` | `ui::setRole(warningLabel_, Warning)` |
| `properties_panel.cpp:28-37` | marges `space3/space2`, `header_` `role=heading` |
| `help_dialogs.cpp:409-411` | `role=title` ; `ai_*_dialog` QGroupBox → `ui::styleGroupBox` |
| `ruler.cpp:35-37` | `QFont` `fontSmall` du token (la règle se repeint sur `changed`) |
| `canvas_view.cpp:1091` `Qt::white` ; `:118-121` | `tokens().canvasPaper` ; `fontSmall`/`weightSemibold` |
| `ui_icons.cpp:20, :183` | §2.5 |
| `ai_segmentation_dialog.cpp:447` | `tokens().canvasMask` |
| Marges/espacements numériques (`workflow_panel`, `document_panel`, `properties_panel`, `empty_state`) | `space*` (migration opportuniste, pas exigée par le garde) |

**Garde `tests/check_no_hardcoded_colors.cmake`** (CTest `check_no_hardcoded_colors`, sans Qt ; modèle : `check_selection_single_mutator.cmake`,
même gestion des commentaires `//` `/* */`, LF/CRLF, numéros de ligne). Candidats : `apps/desktop/*.cpp|hpp` sauf `design_tokens.cpp`. Motifs
**interdits** (hors commentaires) : (a) `#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?` ou `"#[0-9A-Fa-f]{3,4}"` dans un littéral ; (b) `QColor(` suivi d'un
nombre/hexa (`QColor\(\s*(0[xX][0-9A-Fa-f]+|[0-9]+)`) ou d'un littéral chaîne ; `QColor::fromRgb(F)?/fromHsv/fromHsl/qRgb(a)?(` à argument numérique ;
(c) `rgba?\(\s*[0-9]` dans un littéral ; (d) couleurs nommées `Qt::(white|black|red|darkRed|green|darkGreen|blue|darkBlue|cyan|magenta|yellow|gray|darkGray|lightGray|dark…)`
(`Qt::transparent`, `Qt::color0/1` OK). **Non visés** : `QColor(var,var,var)` (données de fil). **Dérogation** : `// color-ok: <raison non vide>` sur la ligne
(plafonnée à 3 au total par le script). **Cliquet** : `tests/color_guard_baseline.txt` (`fichier nombre`) ; échec si le nombre d'un fichier **dépasse** la ligne de base
ou si un fichier absent de la base a ≥ 1 occurrence ; échec aussi si le nombre est **inférieur** (message : « abaisser la base ») → la base converge
vers vide. Base initiale (T2) = comptes actuels : `main_window.cpp 12`, `ui_icons.cpp 2`, `canvas_view.cpp 1`, `ai_segmentation_dialog.cpp 1`.
**Auto-test** `tests/check_color_guard_selftest.cmake` + `tests/fixtures/color_guard/{clean,violations}.cpp.txt` (≥ 25 lignes marquées `// VIOLATION`,
dont `"#8a5a00"`, `QColor(80,120,200)`, `QColor(0xB0,0x30,0x30)`, `Qt::white`, `rgba(1,2,3,4)`, hex 8 chiffres, ligne continuée sur 2 lignes ;
la fixture propre contient `QColor(r,g,b)` variable, `Qt::transparent`, `// #ff0000` en commentaire, `"#include"`, `// color-ok: swatch`) ;
rejoué en LF et CRLF, signale exactement les lignes marquées (aucun faux négatif/positif), comme `check_selection_guard_selftest.cmake`.

## 4. Vérification visuelle headless (personne n'ouvre l'application)

`tests/unit/desktop/test_ui_snapshot.cpp` — suite QTest `test_ui_snapshot` (classe `openstitch::desktop::MainWindowTest` pour l'accès `friend`,
`main_window.hpp:69`, comme `test_ui_adversarial.cpp:2191`), `QT_QPA_PLATFORM=offscreen` via `openstitch_add_qt_test`. **Aucune comparaison de pixels** ;
sortie = artefacts pour relecture humaine/agent.
- `OPENSTITCH_UI_SNAPSHOT_DIR=<dossier>` : si défini, écrit les PNG ; sinon rend dans un `QTemporaryDir` (le test sert alors de fumée anti-crash).
  `OPENSTITCH_UI_SNAPSHOT_SCALE` (1 défaut, 2 pour HiDPI) → `qputenv("QT_SCALE_FACTOR")` impossible après démarrage : **CTest enregistre deux entrées**
  `ui_snapshot` et `ui_snapshot_hidpi` (`ENVIRONMENT QT_SCALE_FACTOR=2`, label `snapshot`).
- Boucle : thème {clair, sombre} × densité {confortable, compact} (via `AppTheme::setMode/setDensity` après `applyToApp`) ; sortie
  `<dir>/<theme>-<densite>/<scène>.png` via `QWidget::grab()` ; fenêtre principale 1280×800 `show()` + `QApplication::processEvents()` (jamais de `sleep`).
- **Scènes** : `main-empty` (état vide) ; `main-project` (projet `buildFixture()` étendu : 1 région, 1 contour, 1 tatami, 1 satin à rails A/B ; un objet
  sélectionné ; docks Document/Propriétés/Workflow/Ordre visibles — copier le constructeur de `test_main_window.cpp:71-130` ; option
  `OPENSTITCH_UI_SNAPSHOT_PROJECT=<.osp>` → `openProjectFile`) ; `main-satin-edit` (rails A/B, poignées, accroche) ; dialogues autonomes `dialog-import`
  (`ImportDialog`), `dialog-gestures`, `dialog-quickstart`, `dialog-ai-preferences`, `dialog-brightness` ; `dialog-generation-options` (via
  `QTimer::singleShot(0)` + `QApplication::activeModalWidget()->grab()` puis `reject()`, schéma de `test_generation_options.cpp:18-30`) ;
  `widget-gallery` : fenêtre synthétique montrant **tous** les contrôles du §2.3 (4 variantes de bouton × {normal, désactivé, coché, focus via `setFocus`},
  cases/radios cochés ou non, curseur, progression, onglets, combo ouvert, spin, champ + placeholder, table, arbre, liste avec sélection, `QGroupBox`,
  `QMenu` ouvert et `QToolTip` (`QToolTip::showText` puis `QWidget` de classe `QTipLabel` ; absent ⇒ scène sautée avec `QWARN`), barres de défilement, `QToolBar`,
  titre de dock, étiquettes `role=…`).
- **`manifest.json`** (même dossier) : Qt runtime/compilé, famille de police effectivement résolue (`QFontInfo`), DPR, liste des fichiers (taille, dimensions) ; et
  **`contrast.md`** : tableau généré depuis `contrast_rules()` (3 colonnes : paire, min, ratio par thème) pour relire les valeurs.
- Assertions de **fumée** seulement : chaque PNG non nul, ≥ 8 couleurs distinctes (pas une page blanche), aucune alerte `qWarning` « Could not parse
  stylesheet »/« Unknown property » (gestionnaire de messages), durée de `setMode` journalisée (`qInfo`), jamais assertée.
- Usage Lead : `OPENSTITCH_UI_SNAPSHOT_DIR=/tmp/ui ctest --test-dir build/linux-qt -R ui_snapshot` ou exécution directe du binaire. Limite : polices
  Linux ≠ Segoe UI, pas de DWM/ombres Windows : les PNG valident couleurs, formes, espacements et débordements, pas la métrique Windows finale (CI Windows + essai manuel).

## 5. Tests (QTest headless ; les suites existantes restent vertes sans modification)

1. `test_design_tokens` (`QTEST_APPLESS_MAIN`, pur) : pour {clair, sombre} × {confortable, compact}, **chaque** `ContrastRule` ≥ `minRatio` (message =
   nom, ratio, thème) ; formule de référence (noir/blanc = 21,0 ; `#767676` sur blanc ≈ 4,54 ; alpha composé) ; tous les rôles de couleur valides et opaques sauf la liste
   explicite de ceux avec alpha ; `space1<…<space7` ; `controlHeight ≥ 24` ; la table `contrast_rules()` couvre chaque token texte/état (réflexion manuelle listée).
2. `test_app_style` (pur) : `build_stylesheet` pour les 4 combinaisons : aucun `@…@` résiduel ; accolades équilibrées ; **aucune règle à corps vide** ; chaque
   placeholder du gabarit ∈ `token_map` ; chaque sélecteur de §2.3 présent (liste attendue dans le test) ; `build_palette` : `Window/Base/Text/Highlight/Disabled*` = tokens.
3. `test_design_system` (`QApplication`, offscreen) : (a) `applyToApp` ⇒ `qApp->style()->name()` = « fusion » (insensible à la casse) ; (b) QSS appliqué à un widget
   témoin avec gestionnaire de messages ⇒ **zéro** `qWarning` (« Could not parse stylesheet », « Unknown property ») ; (c) bascule thème clair↔sombre et densité avec
   `MainWindow` construite + affichée : pas de crash, `qApp->palette().color(Window)` = `window` du thème, `QPushButton::minimumSizeHint().height() ≥ controlHeight` en confortable et
   plus petit en compact, `AppTheme::changed` émis une fois par bascule (`QSignalSpy`), idempotence (`setMode` identique = 0 signal) ; (d) contrat `variant`/`role` :
   `ui::setVariant` pose la propriété, valeur inconnue sans crash, `QDialogButtonBox` Ok = `:default` ; (e) `ThemeChoice::System` : sous Qt ≥ 6.5 suit `colorScheme()`, sous 6.4 retombe sur
   la valeur de démarrage (`#if` dans le test) ; persistance `ui/theme` aller-retour ; (f) icônes : `icons::save().pixmap(32)` a sa couleur dominante opaque = `tokens().icon`
   (clair) puis la nouvelle après `setMode(Dark)` (couleur lue dans le pixmap, **pas** de golden) ; `pixmap(QSize(32,32), dpr 2)` ⇒ 64×64 px ; mode `Disabled` = `textDisabled`.
4. `test_ui_snapshot` / `ui_snapshot_hidpi` : §4.
5. `check_no_hardcoded_colors` + `check_color_guard_selftest` : §3. Enregistrés dans `tests/CMakeLists.txt` à côté de `check_selection_*` (lignes 14-26).
6. Non-régression : `ctest -R "desktop|test_ui|test_main_window|test_canvas|test_properties|test_document|test_help|test_generation|test_node|test_interaction|test_selection|test_wsl|test_sam"`
   (les 14+ exécutables listés dans `tests/unit/desktop/CMakeLists.txt:35-48`, dont `test_ui_adversarial`) vert avant/après chaque tâche ; `clang-format --dry-run --Werror` ; build `linux-core` inchangé.

## 6. Découpage en tâches, risques, décisions

### 6.1 Ordre et propriété exclusive des fichiers (un seul écrivain de `main_window.cpp` : T4, en dernier)

T1 → (T2 ∥ T3, fichiers disjoints) → T4.

**T1 — Tokens, contraste, QSS, thème** (M). Possède : `design_tokens.{hpp,cpp}`, `app_theme.{hpp,cpp}`, `app_style.{hpp,cpp}` (nouveaux), `style_assets.{hpp,cpp}`
(nouveaux), `ui_style.hpp` (nouveau), `main.cpp`, `apps/desktop/CMakeLists.txt`, `tests/unit/desktop/{test_design_tokens,test_app_style,test_design_system}.cpp` +
`tests/unit/desktop/CMakeLists.txt` (lignes de ses 3 cibles seulement).
Acceptation : (1) `Tokens` étendu, anciens champs/noms intacts ⇒ `main_window.cpp` et tous les autres fichiers **compilent sans modification** ; (2) `test_design_tokens`
0 échec sur 4 combinaisons, avec les valeurs du §1.1 (ou ajustées, jamais en baissant un minimum) ; (3) `test_app_style` et `test_design_system` verts, 0 `qWarning` de QSS ;
(4) `qApp->style()->name()` = fusion après `applyToApp` ; (5) `ThemeChoice` + `#if` 6.5 compilent en Qt 6.4.2 (dev) ET sont revus pour 6.8.3 (CI) ; (6) toutes les suites existantes vertes ;
(7) `ui::setVariant/setRole` + `styleGroupBox` disponibles pour T3/T4.

**T2 — Outils de vérification : snapshot + garde de couleurs** (M ; démarre après T1). Possède : `tests/unit/desktop/test_ui_snapshot.cpp` + sa ligne CMake,
`tests/check_no_hardcoded_colors.cmake`, `tests/check_color_guard_selftest.cmake`, `tests/color_guard_baseline.txt`, `tests/fixtures/color_guard/`, `tests/CMakeLists.txt`.
Acceptation : (1) `OPENSTITCH_UI_SNAPSHOT_DIR` produit ≥ 4 dossiers thème×densité × ≥ 12 scènes + `manifest.json` + `contrast.md` ; (2) fumée verte ; (3) garde : passe sur le dépôt
avec la base initiale, échoue si on ajoute `QColor(1,2,3)` dans un `.cpp` de `apps/desktop` (démontré par le test d'auto-contrôle) ; (4) auto-test ≥ 25 motifs, LF et CRLF ;
(5) **le Lead regarde les PNG** de `main-project`/`widget-gallery` avant de lancer T3/T4 (point de contrôle humain).

**T3 — Widgets, panneaux, icônes, canevas** (M ; après T1, parallèle à T2). Possède : `ui_icons.{hpp,cpp}`, `empty_state_widget.cpp`, `import_dialog.cpp`, `workflow_panel.cpp`,
`properties_panel.cpp`, `document_panel.cpp`, `help_dialogs.cpp`, `ruler.cpp`, `canvas_view.cpp`, `ai_segmentation_dialog.cpp`, `ai_preferences_dialog.cpp`,
`brightness_dialog.cpp`, `generation_options_dialog.cpp`, `main_window_directional.cpp` (si besoin) — **jamais `main_window.cpp`**.
Acceptation : (1) icônes via `TokenIconEngine`, test §5.3(f) vert ; (2) `canvas_view.cpp:1091` → `canvasPaper` (grille visible en sombre : test de rendu d'un pixel de grille sur papier ≠ papier) ;
(3) plus de `setStyleSheet` ni de couleur littérale dans ces fichiers ; mise à jour de `tests/color_guard_baseline.txt` (seule édition hors périmètre autorisée, ligne par fichier) ;
(4) suites desktop existantes vertes ; (5) snapshots relus : états vide, import, aide.

**T4 — `main_window.cpp` + documentation** (S/M ; après T2 et T3, **seul écrivain**). Possède : `main_window.cpp`, `main_window.hpp` (si signal/slot de densité), `docs/ui-audit-2026-10.md` §6
(L2 → livré), `docs/ui-redesign-specification.md` §19 (pointeur vers ce plan), `tests/color_guard_baseline.txt` (→ vide).
Acceptation : tableau §3 appliqué ligne à ligne ; menu Affichage ▸ Thème : Clair/Sombre/**Système** ; `grep -nE 'setStyleSheet|QColor\([0-9]|#[0-9a-fA-F]{6}' main_window.cpp` = 0 ;
`color_guard_baseline.txt` vide et garde verte ; icônes de barres à `toolIconSize` et mises à jour sur `changed` ; `ctest --preset msvc-debug` + `linux-qt` complets verts
(dont `test_ui_adversarial`, `test_main_window` 85 tests) ; snapshots finaux relus par le Lead dans les 4 combinaisons.

### 6.2 Risques

1. **Performance QSS** (re-polissage de tout l'arbre à chaque `setStyleSheet`, ~centaines de widgets + `QTableWidget`) : cache par combinaison, mises à jour suspendues, aucune feuille par widget ; durée de bascule journalisée, budget cible < 300 ms sur la fenêtre chargée à vérifier à la main (non assertée : flou en CI).
2. **Fusion modifie les métriques** vs rendu natif : décalages de hauteurs/marges, dialogues à `resize()` fixe (`help_dialogs.cpp:137,355`, `ai_*`) ; `min-height` des contrôles = `controlHeight` ; snapshots compact + 2× pour détecter les débordements.
3. **Glyphes (`image:`)** : coche/chevrons dépendent d'un répertoire de cache et du `@2x` ; repli sans image = état par couleur seule (à surveiller, indice accessibilité).
4. **Windows haute densité** : `QT_SCALE_FACTOR`/échelle fractionnelle (6.8, `PassThrough` par défaut) peut flouter filets 1 px ; non vérifiable ici → essai manuel Windows.
5. **Couleurs du canevas** : papier clair en thème sombre = grand aplat lumineux ; rails A/B quasi isoluminants ; accrocher-halo blanc supposé sur papier clair ; les objets de couleur de fil utilisateur ne sont pas thémés (contenu).
6. **Focus sans décalage** (limite QSS) et `QDialogButtonBox :default` à confirmer par snapshot ; sinon `variant=primary` posé par une aide `ui::stylizeButtonBox`.
7. **Portabilité 6.4/6.8** : `colorScheme`, `QPalette::Accent` sous `#if` ; `QFont::setFamilies` 6.1+ ; vérifier `font-weight` numérique du QSS en 6.8.
8. **Polices** : Segoe UI Variable absente hors Windows 11 → repli `Segoe UI`, puis polices Linux dans l'environnement de dev (métriques différentes).

### 6.3 Décisions prises (à valider/vétoer par l'utilisateur)

D1 Fusion forcé, style Windows natif jamais utilisé ; `-style`/`QT_STYLE_OVERRIDE` ignorés. D2 Rayons 6/10/14/pill ; primaire plein unique, secondaire tonal sans bordure. D3 Accent rouge-brique conservé
(`#B04E3C` / `#E0765F`) ; focus **bleu** distinct de l'accent. D4 Sélection en **teinte** + liseré (plus de rouge plein). D5 Papier du canevas **clair dans les deux thèmes** (`#E4E7EC` en sombre).
D6 Anneau de focus 2 px **sans décalage** (limite QSS). D7 Petites capitales des `QGroupBox` posées en code (`QFont`), seulement 2 groupes aujourd'hui. D8 Pas d'ombres (QSS) : élévation = tons + filet ;
aucun mouvement dans ce lot. D9 Densités 32/26 px, police 9 pt (Segoe UI), tailles en points. D10 `ThemeChoice::System` ajouté mais **défaut = clair** ; sous Qt 6.4 pas de suivi en direct.
D11 Gabarit QSS à placeholders `@token@` en littéral C++ (ni `.qrc` ni fichier externe) ; glyphes rendus au premier lancement dans le cache utilisateur. D12 Garde « cliquet » avec base décroissante et dérogation
`// color-ok:` plafonnée à 3 ; les couleurs de fil (`QColor(var…)`) sont hors garde. D13 `canvasHandle` et `canvasNode` gardent la même valeur (deux tokens pour deux rôles). D14 Les noms
existants de `Tokens` ne sont jamais renommés (pas de migration de masse de `main_window.cpp` avant T4). D15 Bibliothèque SVG, bandeau de notifications, stepper : hors lot (L4/L3/L6).
