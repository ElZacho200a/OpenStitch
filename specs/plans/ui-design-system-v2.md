# Plan : lot L2 — Design system v2 (identité visuelle propre d'OpenStitch)

Status: revision 2 (revue de conception : APPROVE WITH CHANGES, décisions du Lead intégrées — voir « Revision 2 » en fin de document)
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
| Tailles fixes oubliées (revue) | 6 | `ai_segmentation_dialog.cpp:123,129` (280×220), `brightness_dialog.cpp:18` (240), `main_window.cpp:453` (180), `:6715` (120), `:5795` (`setMaximumWidth(48)`) : à valider aux snapshots compact/confortable |
| Widgets déjà tokenisés | — | `canvas_view.cpp:86,103,174,1111-1131`, `workflow_panel.cpp:112`, `help_dialogs.cpp:91,144,396,406`, `main_window.cpp` (≈25 sites `tokens().accent/canvas*`) : à **conserver** (les noms existants de `Tokens` ne sont PAS renommés) |

Surfaces de widgets à habiller (comptes `new X`/`X(` dans `apps/desktop/*.cpp`) : QCheckBox 22, QRadioButton 5, QSlider 3, QProgressBar 1,
QTabWidget 1, QTableWidget/View 11, QTreeWidget 4, QListWidget 6, QComboBox 15, spin 29, QGroupBox 2, QToolButton 2, QDockWidget 6, QToolBar 2.

## 1. Langage visuel (décisions par défaut ; veto possible, cf. §6.3)

**Forme.** Rayons `radiusSm 6 / radiusMd 10 / radiusLg 14 / radiusPill 999` (QSS borne le rayon à la demi-hauteur : à confirmer par snapshot).
Boutons : **un seul primaire plein** (`variant=primary`, fond accent, texte `onAccent`) ; secondaire = **tonal sans bordure** (`tonal`) ;
`ghost` (transparent, texte `text`, fond `tonal` au survol) pour barres d'outils/liens d'action ; `danger` (texte/fond `error`). Champs
(QLineEdit/spin/combo) = fond `surfaceRaised` + bordure 1 px `borderStrong` (≥ 3:1, seul indice de frontière d'un champ), focus = anneau 2 px.
`QGroupBox` **sans cadre** : titre petites capitales (`QFont::SmallCapitals` posé par `ui::styleGroupBox`, QSS ne sait pas) en semi-gras
`textSecondary` + filet `border` 1 px dessous (la prise en compte de `SmallCapitals` dans `QGroupBox::title` est **à prouver sur snapshot** ; repli : texte passé en majuscules par `ui::styleGroupBox`). **Docks** séparés par le contraste `surface` (panneau) / `window` (fond) — pas de traits —
titre de dock sans bordure, `QSplitter::handle`/séparateur de dock 4 px `window`. **Barres de défilement** 8 px, poignée arrondie `borderStrong`
(`border` au repos si ≥ 3:1 impossible : poignée = non-texte, donc `borderStrong`), rails transparents, pas de flèches. **Élévation** :
QSS n'a pas d'ombre portée → niveau 0 `window`, 1 `surface`, 2 `surfaceRaised` + filet `border` 1 px (menus, infobulles, popups de combo) ;
**aucune promesse de coins arrondis pour `QMenu` / popup de `QComboBox`** (fenêtres `Qt::Popup` rectangulaires ; fond translucide hors périmètre : coins carrés + filet) ; l'ombre système des popups Windows est un bonus non garanti (non vérifiable hors écran), aucun `QGraphicsDropShadowEffect`.
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
| `text` / `textSecondary` | `#1B1E23` / `#50575F` | `#E8EAEE` / `#B0B7C1` | texte, aide/légende |
| `textDisabled` ★ | `#7A808A` | `#7C838D` | désactivé (règle maison ≥ 3:1, lisible) |
| `accent` / `Hover` ★ / `Pressed` ★ | `#B04E3C` / `#9A4130` / `#843727` | `#E0765F` / `#E88B77` / `#CF664F` | primaire, case cochée, sélection forte (rouge « fil », conservé) |
| `onAccent` ★ | `#FFFFFF` | `#1C100D` | texte/icône sur `accent*` |
| `tonal` ★ / `Hover` / `Pressed` | `#E4E7EC` / `#D9DDE4` / `#CDD2DA` | `#363B44` / `#40464F` / `#2F343C` | boutons secondaires |
| `selection` / `selectionText` | `#F2DAD3` / `#1B1E23` | `#4B302A` / `#F1F3F6` | **teinte pour lignes de liste/table/arbre uniquement** + liseré gauche 2 px `accent` (indice non-texte) |
| `textSelection` ★ / `textSelectionText` ★ | `#B04E3C` / `#FFFFFF` | `#E0765F` / `#1C100D` | **sélection de texte** = `QPalette::Highlight/HighlightedText` (≥ 3:1 contre `surfaceRaised` ; texte ≥ 4,5:1) ; une teinte pâle serait illisible dans un champ |
| `focus` | `#1F5FC4` | `#6AA3F0` | anneau de focus |
| `icon` ★ | `#50575F` | `#B0B7C1` | icônes (remplace `kInk`) ; actives = `text`, désactivées = `textDisabled` |
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
| `canvasMask` ★ | `#C93F00` | `#C93F00` | surcouche du masque IA (`ai_segmentation_dialog.cpp:447`) |
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

### 1.4 Table de contraste COMPLÈTE (calcul hors dépôt, algorithme §1.3 ; 86 paires × 2 thèmes = 172 contrôles, 0 échec)

Format : `fg` (minimum) : fond clair/sombre. Cette liste est exactement `contrast_rules()` ; `test_design_tokens` régénère `contrast.md` (même format) dans le dossier de snapshots : toute dérive se voit par diff.

- `text` (≥ 4,5) : window 14,26/14,01 · surface 15,45/12,60 · raised 16,71/11,01 · sunken 13,23/14,73 · selection 12,53/9,93 · tonal 13,48/9,34 · tonalHover 12,27/7,90 · tonalPressed 11,00/10,39
- `textSecondary` (≥ 4,5) : window 6,24/8,35 · surface 6,77/7,51 · raised 7,32/6,56 · sunken 5,80/8,78 · tonalHover 5,37/4,71 · tonalPressed 4,82/6,19 · tonal 5,90/5,57
- `success` (≥ 4,5) : window 5,27/7,61 · surface 5,71/6,85 · raised 6,18/5,98 · sunken 4,89/8,00
- `warning` (≥ 4,5) : window 5,06/7,97 · surface 5,48/7,17 · raised 5,93/6,26 · sunken 4,69/8,38
- `error` (≥ 4,5) : window 5,58/6,20 · surface 6,04/5,58 · raised 6,54/4,87 · sunken 5,18/6,52
- `info` (≥ 4,5) : window 6,17/7,46 · surface 6,69/6,71 · raised 7,23/5,86 · sunken 5,73/7,84
- `icon` (≥ 3,0) : window 6,24/8,35 · surface 6,77/7,51 · raised 7,32/6,56 · sunken 5,80/8,78 · tonal 5,90/5,57
- `textDisabled` (≥ 3,0) : window 3,39/4,41 · surface 3,68/3,97 · raised 3,98/3,47 · sunken 3,15/4,64
- `accent` (≥ 3,0) : window 4,48/5,57 · surface 4,85/5,01 · raised 5,25/4,37 · sunken 4,16/5,85 · selection 3,94/3,95
- `focus` (≥ 3,0) : window 5,13/6,52 · surface 5,56/5,86 · raised 6,01/5,12 · sunken 4,76/6,85 · tonal 4,85/4,35
- `textSelectionText` (≥ 4,5) : textSelection 5,25/6,13
- `textSelection` (≥ 3,0) : raised 5,25/4,37 · surface 4,85/5,01
- `onAccent` (≥ 4,5) : accent 5,25/6,13 · accentHover 6,62/7,43 · accentPressed 8,21/5,02
- `selectionText` (≥ 4,5) : selection 12,53/10,76
- `borderStrong` (≥ 3,0) : raised 4,16/3,66 · surface 3,85/4,19 · sunken 3,29/4,90
- `canvasStitch` (≥ 3,0) : canvasPaper 15,73/12,69
- `canvasJump` (≥ 3,0) : canvasPaper 4,72/3,81 · canvasBackground 3,33/3,80
- `canvasNode` (≥ 3,0) : canvasPaper 5,71/4,61 · canvasBackground 4,03/3,14
- `canvasHandle` (≥ 3,0) : canvasPaper 5,71/4,61 · canvasBackground 4,03/3,14
- `canvasRailA` (≥ 3,0) : canvasPaper 4,61/3,72 · canvasBackground 3,26/3,89
- `canvasRailB` (≥ 3,0) : canvasPaper 4,71/3,80 · canvasBackground 3,32/3,81
- `canvasSnap` (≥ 3,0) : canvasPaper 5,61/4,52 · canvasBackground 3,96/3,20
- `canvasPreview` (≥ 3,0) : canvasPaper 5,34/4,31 · canvasBackground 3,77/3,36
- `canvasCutLine` (≥ 3,0) : canvasPaper 5,90/4,76 · canvasBackground 4,17/3,04
- `canvasHoop` (≥ 3,0) : canvasPaper 5,14/4,15 · canvasBackground 3,63/3,49
- `canvasSelectionLine` (≥ 3,0) : canvasPaper 5,25/4,23 · canvasBackground 3,70/3,42
- `canvasSelectionRectLine` (≥ 3,0) : canvasPaper 5,71/4,61 · canvasBackground 4,03/3,14
- `canvasMask` (≥ 3,0) : canvasPaper 5,01/4,04 · canvasBackground 3,53/3,59
- `canvasPaper` (≥ 1,3) : canvasBackground 1,42/14,49

Anciens (échecs corrigés) : `kInk` / surface sombre 2,21 ; `#FF8C00` / blanc 2,33 ; `warning` `B4791F` / blanc 3,69 ; `canvasStitch` sombre `C8CAD8` / papier blanc 1,63. Corrigés en revue : `textSecondary`/`tonalHover` sombre (4,11 → 4,71), `canvasMask`/`canvasBackground` clair (2,88 → 3,53, masque assombri `#C93F00`).

Séparation panneau/fond (non normatif) : `surface`/`window` 1,08 (clair), 1,11 (sombre) — assez pour un dock « posé » sans trait ;
si le snapshot le juge trop faible, assombrir `window` clair à `#E6E9EE` (re-lancer le test).
Placements **interdits** (règle R6, testée) : `canvasMask`, rails, snap, aperçu, coupe, nœuds/poignées uniquement sur `canvasPaper`/`canvasBackground` ; `textDisabled` jamais sur `tonalPressed` ; `textSelection` jamais comme fond de texte non sélectionné.

## 2. Architecture

### 2.1 Fichiers (tous dans `apps/desktop/`, Qt confiné ici)

- `design_tokens.{hpp,cpp}` : `Tokens` étendu (§1), `light_tokens/dark_tokens`, contraste pur (§1.3). **Seule** source de littéraux de couleur.
- **`app_style.{hpp,cpp}`** (nouveau, **pur** : pas de `QApplication`) : `QPalette build_palette(const Tokens&)` (palette **complète** : `Window, WindowText, Base, AlternateBase, Text, Button, ButtonText, BrightText, Light, Midlight, Mid, Dark, Shadow, ToolTipBase/Text, Link (info), LinkVisited, PlaceholderText (textSecondary), Highlight/HighlightedText (= `textSelection`/`textSelectionText`)`, `Accent` sous `#if >= 6.6`, groupe `Disabled` = `textDisabled` pour tous les rôles texte), `QString build_stylesheet(const Tokens&)`,
  `QFont app_font(const Tokens&)`, `QStringList stylesheet_placeholders()` (pour test). Le QSS est un **gabarit unique** (littéral brut
  `R"qss(...)qss"`) à placeholders `@token@` (`@accent@`, `@radiusMd@`, `@space3@`…) substitués depuis **une** table `token_map(const Tokens&)`
  → un nom inconnu laisse un `@x@` détectable ; remplace la concaténation `%1.arg()`. Ni `.qrc` ni fichier externe.
- `style_assets.{hpp,cpp}` (nouveau) : glyphes que le QSS ne peut pas dessiner (coche, point radio, chevrons combo/spin/branche d'arbre). **Aucun fichier écrit
  sur disque** (ni cache utilisateur) : PNG rendus par QPainter→`QBuffer` colorés par tokens (`check.png`, `check@2x.png`…), assemblés **en mémoire** en un blob rcc (format
  « qres » v1, écrit par un petit sérialiseur interne ≤ ~150 lignes ; octets conservés vivants dans un membre statique) puis `QResource::registerResource(const uchar*, root)`
  sous une racine `/openstitch-<n>/` (n incrémenté à chaque bascule ; l'ancienne racine est désenregistrée), référencés dans le QSS par `url(":/openstitch-<n>/check.png")` —
  **toujours entre guillemets**. Échec d'enregistrement ⇒ QSS généré sans `image:` (boîte pleine = état par couleur seule, loggué) ; **ce repli est testé** (crochet de test
  `StyleAssets::forceFailureForTesting`). Plan B si le sérialiseur dépasse l'enveloppe : PNG pré-rendus par jeu de couleurs dans `glyph_data.cpp` (octets, test de parité aux tokens).
  La sélection auto de `@2x` par QSS reste **à valider sur snapshot 2×**.
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
même **sans** `setStyle`, avec `setUpdatesEnabled(false)` sur les fenêtres de premier niveau pendant l'application, ; **pas de cache de
chaîne QSS** (le coût est le re-polissage, pas la génération) ; **règle `QWidget { … }` large interdite** dans le gabarit (coût de polissage ; sélecteurs par classe). `-style` /
`QT_STYLE_OVERRIDE` utilisateur ignorés (identité imposée ; mentionné dans l'aide, D1). **Tests** : par défaut les suites existantes restent sans thème ; **mais** `AppTheme::setMode/setDensity` appellent `reapply()` qui, dès qu'une `QApplication` existe,
applique palette+QSS (`app_theme.cpp:160-165`) et **persiste** : tout test qui déclenche une action Thème/Densité applique donc silencieusement le QSS. Rendu explicite et sûr : (a) `reapply()`
appelle aussi `ensureFusion` (idempotent, jamais de rendu à moitié thémé) ; (b) tout test touchant au thème isole `QSettings` (déjà fait : `test_main_window.cpp:912-918`,
`test_ui_invariants.cpp:95-102`). **Variantes thémées des suites existantes** : voir §5.7 (MAJOR 1). `test_design_system` et le snapshot appellent `applyToApp` explicitement (§4).

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
**Chrome natif (Qt ≥ 6.8, cible CI/Windows 6.8.3)** : à chaque `reapply()` et selon `ThemeChoice`,
```
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(choice == System ? Qt::ColorScheme::Unknown : choice == Dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
#endif
```
pour que la barre de titre Windows et les dialogues natifs suivent le thème d'OpenStitch (`Unknown` rend la main à l'OS). Garde anti-récursion (`applying_`) car `colorSchemeChanged` rappelle `reapply()` ;
`setPalette` est ré-appliqué **après** `setColorScheme` (Qt peut réinitialiser la palette : ordre à confirmer sur snapshot/Windows). Sous 6.4 (env. de dev) : pas de chrome natif thémé ni de suivi en direct
(relu au prochain démarrage) ; documenté dans l'aide du menu. Les `QFileDialog` **restent natifs** (pas de `DontUseNativeDialog`) : D16.
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
(b') **déclarations** `QColor\s+\w+\s*[({]\s*(0[xX]|[0-9])` (ex. `const QColor kInk(0x5C, 0x62, 0x6A)`, `ui_icons.cpp:20`) ; (c) `rgba?\(\s*(%[0-9]|[0-9])` dans un littéral (inclut les formats `rgb(%1,%2,%3)`, `main_window.cpp:3633`) ; (d) couleurs nommées `Qt::(white|black|red|darkRed|green|darkGreen|blue|darkBlue|cyan|magenta|yellow|gray|darkGray|lightGray|dark…)`
(`Qt::transparent`, `Qt::color0/1` OK). **Non visés** : `QColor(var,var,var)` (données de fil). **Dérogation** : `// color-ok: <raison non vide>` sur la ligne
(plafonnée à 3 au total par le script). **Cliquet** : `tests/color_guard_baseline.txt` (`fichier nombre`) ; échec si le nombre d'un fichier **dépasse** la ligne de base
ou si un fichier absent de la base a ≥ 1 occurrence ; échec aussi si le nombre est **inférieur** (message : « abaisser la base ») → la base converge
vers vide. Base initiale (créée par **T3**, qui seul possède le fichier ; T4 n'y abaisse que la ligne `main_window.cpp`, T4 étant sérialisé après T3 ; la ligne CTest de T2 est conditionnelle `if(EXISTS …baseline)`) = comptes actuels : `main_window.cpp 13` (10 `QColor` + 2 hex + `rgb(%1`), `ui_icons.cpp 2`, `canvas_view.cpp 1`, `ai_segmentation_dialog.cpp 1`.
**Auto-test** `tests/check_color_guard_selftest.cmake` + `tests/fixtures/color_guard/{clean,violations}.cpp.txt` (≥ 25 lignes marquées `// VIOLATION`,
dont `"#8a5a00"`, `QColor(80,120,200)`, `QColor(0xB0,0x30,0x30)`, `Qt::white`, `rgba(1,2,3,4)`, `rgb(%1,%2,%3)`, `const QColor kInk(0x5C,0x62,0x6A)`, hex 8 chiffres, `"#ffffff"` en chaîne, ligne continuée sur 2 lignes ;
la fixture propre contient `QColor(r,g,b)` variable, `Qt::transparent`, `// #ff0000` et `/* #abcdef */` en commentaires, `#define FOO 1`, `#include <QColor>`, `// color-ok: swatch`) ;
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
  stylesheet »/« Unknown property » (gestionnaire de messages), durée de `setMode` consignée dans `manifest.json` (champ `setModeMs`), jamais assertée ni chiffrée en budget.
- Usage Lead : `OPENSTITCH_UI_SNAPSHOT_DIR=/tmp/ui ctest --test-dir build/linux-qt -R ui_snapshot` ou exécution directe du binaire. Limite : polices
  Linux ≠ Segoe UI, pas de DWM/ombres Windows : les PNG valident couleurs, formes, espacements et débordements, pas la métrique Windows finale (CI Windows + essai manuel).

## 5. Tests (QTest headless ; les suites existantes restent vertes, + variantes thémées §5.7)

1. `test_design_tokens` (`QTEST_APPLESS_MAIN`, pur) : pour {clair, sombre} × {confortable, compact}, **chaque** `ContrastRule` ≥ `minRatio` (message =
   nom, ratio, thème) ; formule de référence (noir/blanc = 21,0 ; `#767676` sur blanc ≈ 4,54 ; alpha composé) ; tous les rôles de couleur valides et opaques sauf la liste
   explicite de ceux avec alpha ; `space1<…<space7` ; `controlHeight ≥ 24` ; `contrast_rules()` contient les 86 paires de §1.4 dont `textSelection/surfaceRaised` (≥ 3), `textSelectionText/textSelection` (≥ 4,5), `textSecondary/tonalHover|tonalPressed`, `canvasMask/{canvasPaper,canvasBackground}` ; elle couvre chaque token texte/état (réflexion manuelle listée).
2. `test_app_style` (pur) : `build_stylesheet` pour les 4 combinaisons : aucun `@…@` résiduel ; accolades équilibrées ; **aucune règle à corps vide** ; chaque
   placeholder du gabarit ∈ `token_map` ; chaque sélecteur de §2.3 présent (liste attendue dans le test) ; `build_palette` : **tous** les rôles du §2.1 (Window, WindowText, Base, AlternateBase, Text, Button, ButtonText, BrightText, Light, Midlight, Mid, Dark, Shadow, ToolTipBase/Text, Link, LinkVisited, PlaceholderText, Highlight/HighlightedText = `textSelection*`, Accent `#if ≥ 6.6`, groupe Disabled) = tokens attendus ; aucun rôle laissé à la valeur par défaut ; pas de règle `QWidget {` dans le QSS.
3. `test_design_system` (`QApplication`, offscreen) : (a) `applyToApp` ⇒ `qApp->style()->name()` = « fusion » (insensible à la casse) ; (b) QSS appliqué à un widget
   témoin avec gestionnaire de messages ⇒ **zéro** `qWarning` (« Could not parse stylesheet », « Unknown property ») ; (c) bascule thème clair↔sombre et densité avec
   `MainWindow` construite + affichée : pas de crash, `qApp->palette().color(Window)` = `window` du thème, `QPushButton::minimumSizeHint().height() ≥ controlHeight` en confortable et
   plus petit en compact, `AppTheme::changed` émis une fois par bascule (`QSignalSpy`), idempotence (`setMode` identique = 0 signal) ; (d) contrat `variant`/`role` :
   `ui::setVariant` pose la propriété, valeur inconnue sans crash, `QDialogButtonBox` Ok = `:default` ; (e) `ThemeChoice::System` : sous Qt ≥ 6.5 suit `colorScheme()`, sous 6.4 retombe sur
   la valeur de démarrage (`#if` dans le test) ; persistance `ui/theme` aller-retour ; (g) `StyleAssets` : le blob rcc s'enregistre et `QFile(":/openstitch-<n>/check.png")` s'ouvre, désenregistrement à la bascule, **repli** testé (`forceFailureForTesting` ⇒ QSS sans `image:` et sans `qWarning`) ; (h) sous `#if ≥ 6.8` : `styleHints()->colorScheme()` suit `ThemeChoice` ; (f) icônes : `icons::save().pixmap(32)` a sa couleur dominante opaque = `tokens().icon`
   (clair) puis la nouvelle après `setMode(Dark)` (couleur lue dans le pixmap, **pas** de golden) ; `pixmap(QSize(32,32), dpr 2)` ⇒ 64×64 px ; mode `Disabled` = `textDisabled`.
4. `test_ui_snapshot` / `ui_snapshot_hidpi` : §4.
5. `check_no_hardcoded_colors` + `check_color_guard_selftest` : §3. Enregistrés dans `tests/CMakeLists.txt` à côté de `check_selection_*` (lignes 14-26).
7. **Variantes thémées (MAJOR 1, T2)** : `theme_test_support.hpp` expose `applyThemeFromEnv()` (lit `OPENSTITCH_TEST_APPLY_THEME=<light|dark>-<comfortable|compact>` ; absent = rien ; sinon `QApplication::setStyle("Fusion")` + `AppTheme::applyToApp` après isolation des `QSettings`), appelé en fin d'`initTestCase` des 4 suites `test_main_window`, `test_ui_invariants`, `test_ui_adversarial`, `test_ui_characterization` (coût : une ligne chacune). CTest : entrées `<suite>_themed_light_comfortable` et `<suite>_themed_dark_compact` (les deux densités et les deux thèmes couverts ; label `themed`, 8 entrées). Les assertions dépendantes de la géométrie doivent **rester vertes** : tout échec est corrigé en rendant le code robuste ou l'assertion non incidentelle (jamais d'affaiblissement ni `QEXPECT_FAIL` silencieux) et rapporté au Lead.
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
`tests/check_no_hardcoded_colors.cmake`, `tests/check_color_guard_selftest.cmake`, `tests/fixtures/color_guard/`, `tests/CMakeLists.txt`, `tests/unit/desktop/theme_test_support.hpp` (nouveau) et **une ligne** dans `initTestCase` de `test_main_window.cpp`, `test_ui_invariants.cpp`, `test_ui_adversarial.cpp`, `test_ui_characterization.cpp` + les entrées « thémées » de `tests/unit/desktop/CMakeLists.txt` (§5.7). Ne possède **pas** `color_guard_baseline.txt` (T3).
Acceptation : (1) `OPENSTITCH_UI_SNAPSHOT_DIR` produit ≥ 4 dossiers thème×densité × ≥ 12 scènes + `manifest.json` + `contrast.md` ; (2) fumée verte ; (3) garde : (activée dès que T3 crée la base) passe sur le dépôt, échoue si on ajoute `QColor(1,2,3)` dans un `.cpp` de `apps/desktop` (démontré par le test d'auto-contrôle) ; (4) auto-test ≥ 25 motifs, LF et CRLF ;
(5) **porte mesurable** : `manifest.json` liste ≥ 48 fichiers (4 combinaisons × ≥ 12 scènes), aucun de taille nulle, aucun avertissement de débordement (`sizeHint() > minimumSize()` du dialogue ou widget plus grand que son parent après `show()`, relevé par la scène) ; (6) variantes thémées de §5.7 vertes ; (7) **puis** le Lead regarde les PNG de `main-project`/`widget-gallery` avant T3/T4.

**T3 — Widgets, panneaux, icônes, canevas** (M ; après T1, parallèle à T2). Possède : `ui_icons.{hpp,cpp}`, `empty_state_widget.cpp`, `import_dialog.cpp`, `workflow_panel.cpp`,
`properties_panel.cpp`, `document_panel.cpp`, `help_dialogs.cpp`, `ruler.cpp`, `canvas_view.cpp`, `ai_segmentation_dialog.cpp`, `ai_preferences_dialog.cpp`,
`brightness_dialog.cpp`, `generation_options_dialog.cpp`, `main_window_directional.cpp` (si besoin) — **jamais `main_window.cpp`**.
Acceptation : (1) icônes via `TokenIconEngine`, test §5.3(f) vert ; (2) `canvas_view.cpp:1091` → `canvasPaper` (grille visible en sombre : test de rendu d'un pixel de grille sur papier ≠ papier) ;
(3) plus de `setStyleSheet` ni de couleur littérale dans ces fichiers ; **crée** `tests/color_guard_baseline.txt` (comptes restants : `main_window.cpp 13` seulement) ; `workflow_panel.cpp:88` (`dot(palette().color(QPalette::Mid))`) utilise `state_color(NotStarted, tokens)` ; `ruler.cpp:48,57` (`palette().window()/WindowText`) vérifiés (repeints par `ApplicationPaletteChange`, aucune connexion à `changed`) ;
(4) suites desktop existantes vertes ; (5) snapshots relus : états vide, import, aide.

**T4 — `main_window.cpp` + documentation** (S/M ; après T2 et T3, **seul écrivain**). Possède : `main_window.cpp`, `main_window.hpp` (si signal/slot de densité), `docs/ui-audit-2026-10.md` §6
(L2 → livré), `docs/ui-redesign-specification.md` §19 (pointeur vers ce plan), `tests/color_guard_baseline.txt` (ligne `main_window.cpp` → 0, fichier vide).
Acceptation : tableau §3 appliqué ligne à ligne ; menu Affichage ▸ Thème : Clair/Sombre/**Système** ; `grep -nE 'setStyleSheet|QColor\([0-9]|#[0-9a-fA-F]{6}' main_window.cpp` = 0 ;
`color_guard_baseline.txt` vide et garde verte ; icônes de barres à `toolIconSize` et mises à jour sur `changed` ; `ctest --preset msvc-debug` + `linux-qt` complets verts
(dont `test_ui_adversarial`, `test_main_window` 85 tests) ; snapshots finaux relus par le Lead dans les 4 combinaisons.

### 6.2 Risques

1. **Performance QSS** (re-polissage de tout l'arbre à chaque `setStyleSheet`, ~centaines de widgets + `QTableWidget`) : mises à jour suspendues, aucune feuille par widget, aucune règle `QWidget {}` large ; durée de bascule consignée dans `manifest.json` (aucun budget annoncé ni asserté).
2. **Fusion modifie les métriques** (hauteur de contrôle confortable 28 → 32 px, `design_tokens.cpp:16` : à relire dans l'inspecteur `properties_panel.cpp` et les contenus de dock, longs formulaires) ; tailles fixes à revoir : `ai_segmentation_dialog.cpp:123,129` (`setMinimumSize(280,220)`), `brightness_dialog.cpp:18` (`setMinimumWidth(240)`), `main_window.cpp:453` (180), `:6715` (120), `:5795` (`setMaximumWidth(48)` du spin polygone) ; vs rendu natif : décalages de hauteurs/marges, dialogues à `resize()` fixe (`help_dialogs.cpp:137,355`, `ai_*`) ; `min-height` des contrôles = `controlHeight` ; snapshots compact + 2× pour détecter les débordements.
3. **Glyphes (`image:`)** : coche/chevrons passent par un blob rcc en mémoire (sérialiseur maison, repli testé) et dépendent du `@2x` (à valider sur snapshot 2×) ; repli sans image = état par couleur seule (à surveiller, indice accessibilité).
4. **Windows haute densité** : `QT_SCALE_FACTOR`/échelle fractionnelle (6.8, `PassThrough` par défaut) peut flouter filets 1 px ; non vérifiable ici → essai manuel Windows.
5. **Couleurs du canevas** : papier clair en thème sombre = grand aplat lumineux ; rails A/B quasi isoluminants ; accrocher-halo blanc supposé sur papier clair ; les objets de couleur de fil utilisateur ne sont pas thémés (contenu).
6. **Focus sans décalage** (limite QSS) et `QDialogButtonBox :default` à confirmer par snapshot ; sinon `variant=primary` posé par une aide `ui::stylizeButtonBox`.
7. **Portabilité 6.4/6.8** : `colorScheme`, `QPalette::Accent` sous `#if` ; `QFont::setFamilies` 6.1+ ; vérifier `font-weight` numérique du QSS en 6.8.
8. **Polices** : Segoe UI Variable absente hors Windows 11 → repli `Segoe UI`, puis polices Linux dans l'environnement de dev (métriques différentes).

### 6.3 Décisions prises (à valider/vétoer par l'utilisateur)

D1 Fusion forcé, style Windows natif jamais utilisé ; `-style`/`QT_STYLE_OVERRIDE` ignorés. D2 Rayons 6/10/14/pill ; primaire plein unique, secondaire tonal sans bordure. D3 Accent rouge-brique conservé
(`#B04E3C` / `#E0765F`) ; focus **bleu** distinct de l'accent. D4 Sélection en **teinte** + liseré (plus de rouge plein). D5 *(veto-able)* Papier du canevas **clair dans les deux thèmes** (`#E4E7EC` en sombre : grand aplat lumineux mais fils sombres lisibles). Alternative : papier sombre en thème sombre ⇒ re-vérifier la visibilité des couleurs de fil sombres et rebaser grille/stitch/halos.
D6 Anneau de focus 2 px **sans décalage** (limite QSS). D7 Petites capitales des `QGroupBox` posées en code (`QFont`), seulement 2 groupes aujourd'hui. D8 Pas d'ombres (QSS) : élévation = tons + filet ;
aucun mouvement dans ce lot. D9 Densités 32/26 px, police 9 pt (Segoe UI), tailles en points. D10 `ThemeChoice::System` ajouté mais **défaut = clair** ; sous Qt 6.4 pas de suivi en direct.
D11 Gabarit QSS à placeholders `@token@` en littéral C++ (ni `.qrc` ni fichier externe) ; glyphes rendus et enregistrés en mémoire (aucun fichier). D12 Garde « cliquet » avec base décroissante et dérogation
`// color-ok:` plafonnée à 3 ; les couleurs de fil (`QColor(var…)`) sont hors garde. D13 `canvasHandle` et `canvasNode` gardent la même valeur (deux tokens pour deux rôles). D14 Les noms
existants de `Tokens` ne sont jamais renommés (pas de migration de masse de `main_window.cpp` avant T4). D16 *(veto-able)* `QFileDialog` natif conservé (pas de `DontUseNativeDialog`) : chrome natif thémé seulement via `setColorScheme` (Qt ≥ 6.8). D17 Sélection de texte = pleine couleur (`textSelection`), teinte pâle réservée aux lignes. D15 Bibliothèque SVG, bandeau de notifications, stepper : hors lot (L4/L3/L6).

## Revision 2 (revue de conception : APPROVE WITH CHANGES, décisions du Lead)

1. **Thémé en test (MAJOR 1)** : variantes `*_themed_*` des 4 suites existantes (env `OPENSTITCH_TEST_APPLY_THEME`, T2) ; `setMode` applique le QSS dès qu'une `QApplication` existe (`app_theme.cpp:160-165`) — explicité §2.2.
2. **Sélection de texte (MAJOR 2)** : tokens `textSelection`/`textSelectionText` (Highlight), teinte pâle réservée aux lignes ; règles ajoutées (5,25/4,37 contre `surfaceRaised`, 5,25/6,13 texte).
3. **Chrome natif (MAJOR 3)** : branche `#if ≥ 6.8` `setColorScheme`, limite 6.4 documentée, `QFileDialog` natif gardé (D16).
4. **Contrastes** : `textSecondary` (`#50575F` / `#B0B7C1`) et `icon` recalés, `canvasMask` = `#C93F00` ; 86 paires × 2 thèmes, 0 échec (script rejoué).
5. **Table complète** embarquée §1.4 (= `contrast_rules()`, régénérée en `contrast.md`).
6. **Garde** : déclarations `QColor nom(0x…)`, formats `rgb(%1…)`, fixtures `#define`/`#include`/commentaires/chaînes.
7. **Baseline** : propriété de T3 ; T4 n'abaisse que sa ligne ; CTest conditionnel côté T2.
8. **Cache QSS supprimé**, règle `QWidget{}` interdite, aucun budget de 300 ms (temps dans `manifest.json`).
9. **Popups** : aucune promesse de coins arrondis. 10. **Glyphes** : blob rcc en mémoire, `url(":/…")` entre guillemets, repli testé, `@2x` à valider.
11. **Palette complète**, `workflow_panel.cpp:88` et `ruler.cpp:48,57` traités, tests élargis. 12. **Petites capitales** à prouver, repli majuscules.
13. **Porte T2 mesurable** (≥ 48 fichiers non vides, 0 débordement). 14. **Tailles fixes** ajoutées à l'inventaire et aux risques. 15. **D5** et **D1** explicités/vétoables.
