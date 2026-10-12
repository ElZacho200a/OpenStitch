# Vectorisation

Public : utilisateur avancé, développeur. État : **Implémenté** (édition de nœuds
limitée au déplacement).

## But

Transformer une région (masque de pixels) en **contours vectoriels propres** :
contour extérieur et trous, en coordonnées physiques (µm), simplifiés et
nettoyés.

## Algorithme

1. Construction du **masque binaire** de la région.
2. Extraction des contours et trous par `cv::findContours` (mode `RETR_CCOMP`,
   approximation `CHAIN_APPROX_SIMPLE`) — algorithme de Suzuki-Abe.
3. Conversion des points en **coordonnées physiques** (µm, origine au centre de
   l'image, Y vers le haut).
4. **Simplification** Douglas-Peucker par contour, avec une tolérance
   **adaptative** bornée à `périmètre / 16` : les petits trous (mât, cordage
   traversant une voile) ne sont pas aplatis au point de disparaître.
5. **Nettoyage** par union Clipper2 (`clean_to_path_sets`) qui reconstruit la
   hiérarchie extérieur/trous (règle pair-impair) et corrige les
   auto-intersections. Une région en plusieurs morceaux produit plusieurs
   `PathSet`.

### Variante courbes de Bézier (`curve_tolerance`)

Option `VectorizeOptions::curve_tolerance` (> 0 ; défaut 0 = polygones, rien ne
change pour les projets existants). Les étapes 1 à 3 et le nettoyage Clipper2
sont identiques, mais le contour dense n'est **pas** simplifié par
Douglas-Peucker : il est ajusté par `geometry::fit_curves` *après* le
nettoyage (la topologie extérieur/trous est déjà corrigée).

`fit_curves` implémente l'ajustement de Schneider (*An Algorithm for
Automatically Fitting Digitized Curves*, Graphics Gems, 1990) : 1) détection des
**coins** (changement de direction mesuré sur une fenêtre d'arc de part et
d'autre du sommet, maximum local, seuil 55° par défaut) ; 2) par tronçon entre
coins : moindres carrés sur la longueur des poignées, reparamétrisation de
Newton-Raphson, découpe récursive au point d'écart maximal ; 3) l'écart est
mesuré **géométriquement dans les deux sens** (données → courbe et courbe →
données), pas seulement sur le paramètre. Les parties droites restent des
segments sans tangente ; les jonctions internes sont des nœuds *Lisses*, les
coins des nœuds *Coin*. Écrit d'après la publication (Potrace, sous GPL, n'est
ni utilisé ni copié). Déterministe.

Mesures (tests `test_curve_fit`, `test_vectorize`) sur un disque rastérisé à
96 dpi : à tolérance égale de 1,5 px (0,4 mm) ou plus, le nombre de nœuds est
divisé par **≥ 3** par rapport à Douglas-Peucker (critère HP-AUTO-004) et l'écart
au vrai cercle reste dans la tolérance. **Limite :** le contour de pixels est un
escalier de ±0,5 px ; une tolérance inférieure à ~1 px ne fait qu'ajuster ce bruit
(à 1,2 px le gain tombe vers ÷2). Le raffinement par contours actifs
(Álvarez–Morel 2026) et la bissection/symétrie (Zhu–You–Zhang 2023) ne sont pas
implémentés.

Les remplissages (tatami, directionnel, auto-satin, mesures d'aire) lisent les
positions de nœuds : `generate.cpp` aplatit les courbes avant (`geometry::
flatten_to_polygon`, tolérance 50 µm), et la couche Clipper2 de `geometry` ainsi
que `signed_area_um2` font de même. Ces fonctions sont sans effet sur un
chemin sans tangente. Effet connu : sur une forme réellement ronde, les rangées
de tatami proches de la tangente produisent davantage de points courts
(< 0,5 mm) qu'avec un contour à facettes. Option CLI : `digitize --curve-tolerance`.

## Résultat

Un `document::VectorObject` contient un ou plusieurs `geometry::PathSet`
(`outer` + `holes`), garde la couleur de la région et un lien vers la
`RegionId` d'origine.

## Formes dessinées à la main

*État : Présent · Testé (QTest headless) · Validé visuellement : non (capture
d'écran non disponible dans cet environnement — voir *Limitations*).* Activé
par défaut, aucun réglage.

Second chemin de création d'un `VectorObject`, indépendant de toute image :
avant cette fonctionnalité, la seule façon d'obtenir un objet vectoriel était
de vectoriser une région segmentée depuis une image importée — un logiciel de
digitalisation classique (Hatch et équivalents) permet aussi de dessiner
directement une forme de base sur le canevas. Quatre outils dans la palette
(barre d'outils gauche) :

- **Rectangle** (`R`) : glisser un cadre élastique → rectangle à 4 coins droits.
- **Ellipse** (`O`, Maj = cercle) : glisser un cadre élastique englobant →
  ellipse à 4 nœuds **lisses** (tangentes de Bézier, constante de Kappa
  ≈ 0,5523 — approximation standard, erreur radiale relative maximale
  ~0,027 %). Maj enfoncée pendant le glisser : le côté le plus petit du cadre
  contraint l'autre → cercle.
- **Polygone** (`P`) : clics successifs posent les sommets (aperçu élastique
  jusqu'au curseur) ; double-clic pour fermer (minimum 3 sommets, sinon le
  tracé est abandonné sans rien créer) ; Échap annule le tracé en cours à tout
  moment, y compris en changeant d'outil.
- **Forme libre / lasso** (`L`) : glisser en continu → un point par évènement
  de déplacement souris pendant que le bouton reste enfoncé (aperçu élastique
  mis à jour à chaque point) ; au relâchement, le tracé brut est **simplifié**
  (Douglas-Peucker, tolérance 0,3 mm — cf. *Vectorisation*, même algorithme que
  la simplification des contours vectorisés) pour ne garder qu'un contour à
  angles droits exploitable, sans lissage. Moins de 3 sommets après
  simplification (tracé trop court, ou entièrement colinéaire) → rien n'est
  créé, message en barre de statut.

La forme créée est un `VectorObject` **sans région source**
(`source_region = std::nullopt` — le champ est optionnel précisément pour ce
cas), automatiquement sélectionné : l'utilisateur enchaîne avec les actions
**Créer un…** existantes (menu Broderie) pour la convertir en objet de
broderie, exactement comme pour une forme vectorisée depuis une image. Aucun
nouveau chemin de création côté broderie : le pipeline aval (génération,
routage, export DST) est intégralement réutilisé.

Géométrie construite par `libs/geometry/src/primitives.cpp`
(`rectangle_path`/`ellipse_path`/`polygon_path`/`freeform_path`, testés
indépendamment de Qt) ; `MainWindow` ne fait que router le geste souris
(glisser pour rectangle/ellipse — mécanique de cadre élastique déjà utilisée
pour le recadrage image, généralisée ; clics successifs pour le polygone ;
glisser continu pour la forme libre) vers ces fonctions puis vers
`AddVectorObjectCommand` (annulable, comme la vectorisation). Un cadre trop
petit (glisser quasi nul) ne crée rien.

**Correctif de revue — Maj = cercle, désormais testable et fiable.** La
détection de Maj (contrainte cercle sur l'ellipse) interrogeait
`QGuiApplication::keyboardModifiers()` (état clavier **global**) au moment de
traiter le signal de fin de glisser — fonctionnellement correct en usage réel
(même thread, traitement synchrone), mais impossible à couvrir par un test
QTest **offscreen** (`QTest::keyPress` sur une fenêtre non affichée ne met pas
à jour cet état de façon fiable), donc jamais réellement vérifié bout en bout.
Corrigé : les modificateurs sont désormais capturés directement sur
l'évènement Qt de relâchement qui termine le glisser
(`CanvasView::mouseReleaseEvent`) et transmis par le signal `boxDrawnMm` —
plus de dépendance à un état global, testable en passant directement
`Qt::ShiftModifier`.

## Édition de nœuds

Les nœuds de l'objet sélectionné s'affichent (poignées de taille constante) et se
**déplacent** à la souris ; chaque déplacement passe par `MoveNodeCommand`
(annulable).

Limitation : l'**ajout/suppression de nœuds**, la **conversion anguleux/lisse**
et l'édition de tangentes de Bézier ne sont **pas** exposés dans l'interface
(le modèle les prévoit : `PathNode` porte `tan_in`/`tan_out` et un `NodeType`).

## Robustesse

La correction de la tolérance de simplification (adaptative) évite un défaut
observé : des trous trop simplifiés faisaient déborder les remplissages (voir
*Tatami* et *Limitations*).

## Implémentation associée

- `libs/vectorization/src/vectorize.cpp` — `vectorize_region`.
- `libs/geometry/src/simplify.cpp` — Douglas-Peucker, aire signée.
- `libs/geometry/src/clean.cpp` — `clean_to_path_sets` (Clipper2 encapsulé).
- `libs/document/include/openstitch/document/vector_object.hpp` — `VectorObject`,
  `NodeRef`.
- `libs/commands/.../project_commands.hpp` — `AddVectorObjectCommand`,
  `MoveNodeCommand`.
- `libs/geometry/include/openstitch/geometry/primitives.hpp` + `src/primitives.cpp` —
  `rectangle_path`, `ellipse_path`, `polygon_path`, `freeform_path` (formes
  dessinées à la main).
- `apps/desktop/main_window.cpp` — `addVectorPrimitive`, `onBoxDrawn`,
  `onCanvasDoubleClicked`, `updatePolygonPreview`/`finishPolygon`/
  `cancelPolygonDraw`, `onFreeformPointAdded`/`finishFreeform`/
  `cancelFreeformDraw` ; `apps/desktop/canvas_view.hpp/.cpp` — modes
  `setBoxDrawMode`/`setPolygonDrawMode`/`setFreeformDrawMode` (généralisation
  du cadre élastique de recadrage) ; `apps/desktop/ui_icons.hpp/.cpp` —
  `icons::freeform`.
- Tests : `tests/unit/vectorization/test_vectorize.cpp`,
  `tests/unit/geometry/test_simplify.cpp`, `test_clean.cpp`,
  `test_primitives.cpp` ; `tests/unit/desktop/test_main_window.cpp` (création,
  undo/redo, cadre dégénéré, tracé de polygone, annulation).
