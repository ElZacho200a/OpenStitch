# Vectorisation

Public : utilisateur avancé, développeur. État : **Implémenté** (édition de nœuds
limitée au déplacement).

## Correctif : poignées de Bézier bornées (2026-10-16)

`geometry::fit_curves` (Schneider) calculait la longueur des poignées par moindres carrés sans borne
haute. Sur une petite boucle irrégulière (contour de pixels dense), le système est presque singulier
et les poignées atteignaient plusieurs mètres : avec `--curve-tolerance 0.4 --dark-lines`, un tatami
envoyait des points à 2 m du motif (largeur du DST 2 130 mm au lieu de 291 mm). Une poignée est
désormais bornée par la longueur d'arc de la polyligne ajustée ; au-delà, repli sur l'heuristique de
Wu/Barsky (corde/3, ou arc/6 pour une boucle sans corde). Test de propriété : 6 000 silhouettes en
escalier x 3 tolérances, aucune poignée plus longue que le périmètre (avant : 6 506 violations, jusqu'à
6,6 fois le périmètre). Navire : largeur 2 130 mm -> 290,6 mm.

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

## Banc des tolérances (VEC-ADAPT-01, lot A)

Bornes communes à Studio et à `vectorize_segmentation` : courbes 0,1–10 px, simplification
0,01–2 mm (`project_ops::kCurveToleranceMinPx`… dans `revectorize.hpp`) ; défauts 1,5 px et 0,2 mm.

Banc caché `test_project_ops "[.vec_adapt_bench]"` (variable `OPENSTITCH_VEC_BENCH_OSP` pour un
OSP réel, jamais versionné). Référence : polygone 0,01 mm (le contour des pixels). Mesures du
2026-11-19, Debug, MonetGasque (341 régions, 0,16 mm/px ; « petites » = aire < 25 mm²) :

| Réglage | Nœuds | Nœuds petites | Écart moyen (µm) | Écart d'aire | Temps |
|---|---:|---:|---:|---:|---:|
| polygone 0,05 mm | 33 410 | 5 969 | 128 | 0,00 % | 149 ms |
| Bézier 1,5 px (défaut) | 6 181 | 1 355 | 215 | 5,6 % | 0,6 s |
| Bézier 0,5 px | 18 610 | 3 268 | 175 | 2,7 % | 1,7 s |
| Bézier 0,25 px | 24 945 | 4 276 | 204 | 3,3 % | 2,1 s |
| Bézier 0,1 px | 25 265 | 4 383 | 195 | 3,3 % | 2,0 s |

Lecture :
- sous 0,5 px, les nœuds triplent ou quadruplent sans gain de fidélité (écart et aire stagnent,
  voire se dégradent) et 0,25 → 0,1 px ne change presque rien : l'ajustement suit les escaliers
  du raster, comme l'annonce `CurveFitOptions` (« au moins ~1,2 px ») ;
- 0,5 px est le seul palier subpixel utile : l'écart d'aire est divisé par deux (5,6 → 2,7 %)
  au prix de ×3 nœuds ;
- sur le corpus synthétique (disque, fleur de 3 mm, pointe, trou de 0,4 mm), même tendance :
  1,5 px → 0,5 px fait passer l'écart moyen de 61 à 42 µm, 0,25/0,1 px n'apportent rien ;
  tous les trous sont conservés à tous les réglages.

Limites : l'écart compare les morceaux par rang (pire Hausdorff approché des contours appariés),
les valeurs maximales sont donc peu fiables quand le nombre de morceaux diffère (le polygone
0,2 mm perd des régions) ; temps en Debug ; aucun effet sur les points ni essai textile mesuré.
Conséquence pour le lot B : une tolérance par région doit descendre vers ~0,5 px sur les petites
formes, pas au-delà, et la reconstruction de pétales (lot D) reste la seule voie sous le pixel.

## Précision intelligente (VEC-ADAPT-01, lot B)

Case « Précision intelligente » du panneau Vecteurs, argument `adaptive` de
`vectorize_segmentation`, `vectorization::AdaptiveTolerance` dans le moteur. **Désactivée par
défaut** : sans geste de l'utilisateur, aucune forme ne change (testé octet pour octet).

Par région : taille caractéristique = min(rayon équivalent √(aire/π), 2 × rayon inscrit), en
pixels (le rayon inscrit vient de la transformée de distance du masque : une pointe fine compte
comme petite même si son aire est grande). Facteur = clamp(taille / 30 px, 1/3, 1), combiné au
facteur des zones de détail (minimum), appliqué aux tolérances polygone et courbes. Avec 1,5 px
de base, une fleur de quelques millimètres descend à 0,5 px, le palier utile mesuré ci-dessus ;
les grandes formes (taille ≥ 30 px) restent identiques. Le plancher de 0,1 mm du polygone reste
en vigueur.

Banc, MonetGasque (341 régions, Debug) :

| Réglage | Nœuds | Nœuds petites | Écart moyen | Écart d'aire | Temps |
|---|---:|---:|---:|---:|---:|
| Bézier 1,5 px | 6 181 | 1 355 | 215 µm | 5,6 % | 0,6 s |
| Bézier 0,5 px global | 18 610 | 3 268 | 175 µm | 2,7 % | 1,9 s |
| **Bézier auto (base 1,5 px)** | **8 411** | 2 823 | **163 µm** | 2,8 % | 0,7 s |
| Polygone 0,2 mm | 9 077 | 2 008 | — | 5,2 % | 76 ms |
| **Polygone auto (base 0,2 mm)** | **12 112** | 3 826 | — | 1,2 % | 75 ms |

Le mode auto atteint la fidélité du réglage global à 0,5 px avec 45 % de ses nœuds : les nœuds
supplémentaires vont aux petites formes.

### Diagnostic et surcharge par région

Le panneau Vecteurs affiche, pour la forme sélectionnée, la tolérance effectivement appliquée à
sa région d'origine (valeur, facteur et origine : réglage de base, précision intelligente ou
surcharge), calculée par `project_ops::region_tolerance` avec le même code que la
vectorisation (`vectorization::region_tolerance_factor`).

Le menu « Précision de la région » enregistre une surcharge : Détail maximum (×1/3), Standard
(×1) ou Simplifier (×2) ; « Réglage global » la retire. La surcharge est une préférence de
l'utilisateur (`ImageSource::vector_precision`, clé OSP `vectorPrecision` écrite seulement si
non vide), posée par `SetRegionVectorPrecisionCommand` (une étape d'annulation) ; elle remplace
le facteur calculé, que la précision intelligente soit cochée ou non, et ne change les formes
qu'à la prochaine mise à jour des formes. Une entrée dont la région a disparu est inerte.

Non fait : adaptation par tronçon (lot C), mémorisation de la case globale dans le projet,
avertissement avant de remplacer un vecteur retouché à la main, effet sur les points et essai
textile.

## Tolérance par tronçon (VEC-ADAPT-01, lot C : expérimental, non exposé dans Studio)

`AdaptiveTolerance::local` (moteur), argument `adaptive_local` de `vectorize_segmentation`
(avec `adaptive` et `curves`). Mode courbes seulement. Largeur locale en chaque sommet = plus
grande valeur de la transformée de distance dans un rayon de 10 px, dans la forme (pointe,
pétale) et dans le fond (encoche) ; facteur clamp(largeur / 10 px, 1/3, 1) arrondi à trois
paliers ; zones fines élargies de 2 sommets, tronçons de moins de 4 sommets absorbés par le
voisin le plus fin. Chaque tronçon est ajusté comme un chemin ouvert et les tronçons partagent
leurs extrémités (contour fermé exact, jonctions en nœuds Coin). Repli sur l'ajustement uniforme
avec un seul palier, si l'orientation s'inverse, ou si la région a une surcharge.

Banc (Debug) :

| Réglage | Nœuds | Écart moyen | Écart max | Écart d'aire | Temps |
|---|---:|---:|---:|---:|---:|
| MonetGasque, Bézier auto (région) | 8 411 | 163 µm | 579 µm | 2,77 % | 0,6 s |
| MonetGasque, Bézier auto + tronçons | 13 643 | 168 µm | 1 180 µm | 2,83 % | 1,8 s |
| Synthétique, Bézier auto (région) | 60 | 55 µm | 73 µm | 3,31 % | 56 ms |
| Synthétique, Bézier auto + tronçons | 117 | 53 µm | 69 µm | 2,45 % | 383 ms |

Décision : **non déployé dans Studio**. Sur le cas réel, +62 % de nœuds et ×3 de temps sans gain
de fidélité (l'écart maximal double, sans doute aux jonctions de tronçons ; non analysé) ; le gain
n'apparaît que sur des formes synthétiques à pointe fine. La fiche du chantier demande de ne
déployer ce mode que si le banc confirme sa valeur. Pistes avant de réessayer : jonctions lisses
(tangentes continues) au lieu de coins, critère de largeur moins sensible aux escaliers, et
comparaison polygone / hybride.

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
