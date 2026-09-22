# Numérisation automatique

Public : utilisateur avancé, développeur. État : **Partiellement implémenté**
(heuristiques, non validées sur machine réelle).

La numérisation automatique (`autodigitize::auto_digitize`) transforme une
segmentation en objets de broderie **éditables** : un objet vectoriel par
région, puis un objet de broderie dont le type (satin, tatami, contour) est
choisi selon la forme (voir *Objets de broderie*). Ce chapitre documente les
réglages qui décident de la qualité du résultat cousu, introduits après
l'audit d'un cas réel (septembre 2026, ci-dessous).

## Cas d'étude : la marine plein cadre

Une image « carte de segmentation » (ciel, nuages, lune, mer en trois tons,
reflet de lune, rivage, deux bateaux, mouettes ; PNG sans alpha, environ
250 × 176 mm) numérisée puis exportée en DST donnait : 38 907 points, aucun
point sur le ciel, tous les tatami à 0° sans sous-couche, des interstices
entre remplissages voisins, 2 916 déplacements dont 1 321 de plus de 3 mm et
aucune coupe, 4 946 points de moins de 0,5 mm, le reflet éclaté en 817
morceaux et aucun point d'arrêt. Chaque section ci-dessous traite une de ces
causes (lots A à G de la mission).

## Fond présumé (Lot A)

Une image sans canal alpha n'a aucun pixel transparent : son fond devient une
région opaque comme les autres, souvent la plus grande.
`AutoOptions::skip_largest_region` exclut alors la **couleur** de la plus
grande région, et toutes les régions de cette couleur exacte (un fond peut
être fragmenté par le motif).

Avant ce lot, l'option était cochée d'office dès que l'image n'avait pas
d'alpha. Sur une image **plein cadre**, la plus grande région est un vrai
élément du motif (le ciel de la marine) : elle n'était pas brodée du tout.

La recommandation vient désormais de `segmentation::background_candidate(seg)`,
qui mesure pour la couleur de la plus grande région :

| Mesure | Sens |
|---|---|
| `area_ratio` | part de l'image couverte par cette couleur (toutes régions) |
| `lightness` | clarté CIELAB L* de la couleur (`cielab_lightness`, calcul analytique D65) |
| `sides_touched` | nombre de bords de l'image touchés (0 à 4) |
| `recommended` | `lightness > min_lightness` (90) **et** `sides_touched >= min_sides_touched` (3) |

Les seuils sont dans `BackgroundCandidateOptions`. Le dialogue *Numérisation
automatique* coche la case seulement si `recommended`, et affiche la pastille
de couleur, le pourcentage de surface, L* et le nombre de bords touchés. Le
CLI (`openstitch-cli digitize`) applique la même règle avec
`--skip-background -1` (défaut) et imprime la candidate ; `0`/`1` restent des
choix explicites prioritaires. Sur la marine, le ciel (bleu, L* ≈ 43) n'est
plus ignoré.

## Orientation et sous-couche des remplissages (Lot B)

Avant ce lot, chaque tatami créé par l'auto-numérisation recevait
`TatamiParams{}` : rangées à 0°, aucune sous-couche. Les valeurs par défaut de
`TatamiParams` **ne changent pas** (projets `.osp` existants, objets créés à
la main) : les réglages sont appliqués par une post-passe
(`configure_tatami_fills`) sur les tatami que l'auto-numérisation crée, y
compris les remplissages de repli d'un satin incomplet, pilotée par des
champs d'`AutoOptions`.

**Angle.** L'orientation des rangées suit l'axe principal de la surface de
l'objet (`geometry::principal_axis` : moments d'inertie du second ordre,
calculés exactement sur le polygone par la formule de Green, trous déduits).
Si le rapport des moments principaux est inférieur à `fill_isotropy_ratio`
(1,3), la forme est jugée quasi isotrope (disque, carré) : l'angle vaut
`isotropic_fill_angle` (45°).

**Voisins.** Deux tatami dont les régions se touchent (au sens de
`segmentation::region_adjacency`) ne doivent pas avoir des rangées à moins de
`min_neighbor_fill_angle_gap` (20°) l'une de l'autre, sinon la frontière
disparaît à l'œil. Les tatami sont traités par aire décroissante (puis
identifiant d'objet, pour un ordre total déterministe) : une grande région
fixe son angle naturel, une plus petite en conflit cherche l'angle le plus
proche du sien (pas de `fill_angle_search_step`, 5°) qui respecte l'écart
avec tous ses voisins déjà placés ; à défaut, celui qui maximise l'écart
minimal. C'est donc toujours la plus petite qui cède.

**Sous-couche** selon l'aire nette de l'objet :

| Aire | Sous-couche |
|---|---|
| < `underlay_edge_min_area_mm2` (20 mm²) | aucune |
| 20 à `underlay_parallel_min_area_mm2` (100 mm²) | contour rentré (`underlay_edge`) |
| ≥ 100 mm² | contour + rangées perpendiculaires (`underlay_parallel`) |

`auto_fill_angle = false` et `auto_fill_underlay = false` rétablissent le
comportement historique. L'import SVG direct (`auto_digitize_vectors`) reçoit
l'angle naturel et la sous-couche, mais pas la règle de voisinage (aucune
segmentation, donc aucune adjacence connue) — limite documentée.

## Chevauchement entre remplissages voisins et ordre en couches (Lot C)

**Cause des interstices.** Trois écarts s'additionnaient le long de chaque
frontière entre deux tatami :

1. la vectorisation trace chaque contour par les **centres** des pixels de
   bord (`vectorize.cpp`) : deux régions voisines sont séparées d'un pixel
   entier (0,17 mm sur la marine) ;
2. chaque contour est simplifié indépendamment (tolérance 0,2 mm) ;
3. chaque tatami est rentré de `TatamiParams::inset` (0,2 mm) de son côté.

**Correction** (`overlap_neighbor_fills`) : la surface remplie d'un tatami
devient

    (région rentrée de inset) ∪ (région ⊕ g ∩ voisins brodés ⊕ g),  g = fill_overlap + ½ pixel

Le débord ne se fait donc que du côté des régions voisines **brodées**, sur
`fill_overlap` (0,3 mm, réglable dans `AutoOptions`) au-delà de la vraie
frontière. Les bords extérieurs du motif et le contact avec le fond ignoré
(qui n'a pas d'objet vectoriel, donc n'est jamais un voisin brodé) gardent le
retrait. La surface élargie remplace la géométrie de l'objet vectoriel de la
région, et `inset` passe à 0 pour ne pas rentrer deux fois. Conséquence
visible : l'objet vectoriel éditable déborde légèrement sur ses voisins.
Toutes les surfaces sont calculées sur la géométrie d'origine avant d'être
appliquées, donc le résultat ne dépend pas de l'ordre de traitement.
`fill_overlap = 0` rétablit l'ancien comportement.

Les remplissages de repli d'un satin incomplet ne sont pas concernés : ils
recouvrent déjà leurs bandes satin (`kCoverageOverlap`).

**Ordre en couches.** Un remplissage qui déborde doit passer **sous** son
voisin : les grandes zones de fond (ciel, mer) doivent être cousues avant les
détails posés dessus. La nouvelle stratégie
`optimization::OrderStrategy::LayeredColorThenProximity` :

- garde le regroupement par couleur (même nombre de changements de fil) ;
- ordonne les couleurs par aire de leur plus grande zone, décroissante ;
- à couleur égale, coud d'abord les grandes zones (aire ≥
  `layer_large_area_ratio` = 25 % de la plus grande de la couleur) par aire
  décroissante, puis les petites par proximité ;
- laisse les objets verrouillés à leur place (mécanisme commun à toutes les
  stratégies).

L'auto-numérisation l'applique à son résultat (`order_by_layers`). L'unité
d'ordre est une suite contiguë d'objets de même `source_vector`, pour que les
sections satin d'une région restent contiguës : `generate_sequence` ne route
ensemble que des sections contiguës. Avant ce lot, aucune numérisation
automatique (desktop ou CLI) n'ordonnait son résultat : les objets suivaient
l'ordre des identifiants de région (classe k-means puis balayage de l'image).

## Fragments (Lot D)

Avant ce lot, le seul seuil de taille était `SegmentationOptions::
min_region_px` (16 px, soit 0,48 mm² à 146,8 dpi : indépendant de l'échelle),
et toute région sous `min_fill_area_mm2` devenait un contour point triple,
même au milieu d'autres régions brodées. Sur la marine, le reflet (couleur
très découpée, anticrénelage) donnait 817 morceaux, dont 683 de moins de 15
points.

`auto_digitize` travaille désormais sur une **copie** de la segmentation (celle
du projet n'est pas modifiée) et la nettoie avant de vectoriser :

1. **Isthmes et lamelles** (`segmentation::remove_thin_parts`) : ouverture
   morphologique région par région, avec un noyau elliptique de
   `min_feature_width_mm` (1,2 mm, converti en pixels avec `mm_per_px`). Les
   pixels retirés sont rendus de proche en proche à la région **voisine**
   majoritaire. Une partie qui ne touche aucune autre région (lamelle isolée
   dans le vide) reste en place.
2. **Petites régions** (`segmentation::merge_small_regions`) : toute région de
   moins de `min_region_area_mm2` (3 mm², converti en pixels) est fusionnée
   avec la voisine qui partage la plus longue frontière. La plus petite
   restante est toujours traitée d'abord, et la région absorbante garde sa
   couleur. Le **fond ignoré n'absorbe jamais** : un fragment qui n'a que lui
   pour voisin reste une région isolée et devient un contour point triple
   (s'il est sous `min_fill_area_mm2`). C'est désormais le seul cas de
   contour pour une petite région.

Les deux étapes sont déterministes (voisinages ordonnés, égalités tranchées
par le plus petit identifiant) et désactivables (seuil à 0).

**Compromis connu** : l'ouverture efface aussi les détails volontaires plus
fins que 1,2 mm (par exemple un mât fin), rendus à la région qui les entoure.
Pour les garder, baissez `min_feature_width_mm`.

## Coupes automatiques et points d'arrêt (Lot E)

Avant ce lot, seules les retouches manuelles produisaient des `Trim`, et les
seuls points d'arrêt étaient ceux, désactivés par défaut, du satin : la
marine comptait 1 321 déplacements de plus de 3 mm sans coupe et aucun point
d'arrêt. Ces finitions ne relèvent pas de l'auto-numérisation mais de la
séquence : elles s'appliquent à tout projet, via `project.finishing`
(`document::SequenceFinishing`) et `stitch_generation::finish_sequence`,
dernière passe d'`effective_sequence` (voir *Moteur de génération de
points*) :

| Réglage | Défaut | Rôle |
|---|---|---|
| `trim_threshold` | 3 mm | au-delà : arrêt de sortie, coupe, déplacement, arrêt d'entrée ; en deçà : simple saut |
| `trim_before_color_change` | oui | coupe avant chaque changement de fil |
| `lock_type` | aller-retour | point d'arrêt à l'entrée/sortie de chaque objet et autour de chaque coupe |
| `lock_length`, `lock_passes` | 0,8 mm, 2 | taille (bornée au point voisin) et répétitions |

Réglables dans le desktop (menu *Broderie ▸ Options de génération…*,
annulable) et dans le CLI (`digitize --trim-threshold <mm> --lock
none|backforth|triangle|zigzag`). Un projet `.osp` antérieur est relu sans
finitions (séquence identique).

## Implémentation associée

- `libs/segmentation/include/openstitch/segmentation/segmentation.hpp` —
  `cielab_lightness`, `BackgroundCandidateOptions`, `BackgroundCandidate`,
  `background_candidate`, `region_adjacency`, `remove_thin_parts`,
  `merge_small_regions` (Lot D).
- `libs/autodigitize/src/autodigitize.cpp` — `auto_digitize`,
  `AutoOptions::skip_largest_region`, `configure_tatami_fills` (Lot B),
  `overlap_neighbor_fills`, `order_in_layers` (Lot C).
- `libs/geometry/include/openstitch/geometry/boolean.hpp` — `union_polygons`.
- `libs/optimization/include/openstitch/optimization/order.hpp` —
  `OrderStrategy::LayeredColorThenProximity`, `OrderOptions`,
  `OrderItem::area_mm2`.
- `libs/geometry/include/openstitch/geometry/moments.hpp` — `principal_axis`.
- `apps/desktop/main_window.cpp` — `MainWindow::autoDigitize` (dialogue).
- `apps/cli/main.cpp` — `run_digitize`.
- Tests : `tests/unit/segmentation/test_segmentation.cpp` (cas « fond
  blanc », « plein cadre coloré », « blanc central », « deux bords »),
  `tests/unit/desktop/test_main_window.cpp`
  (`autoDigitizeDialogDoesNotSkipColoredFullFrameRegion`,
  `autoDigitizeDialogSkipsNearWhiteFramingBackground`),
  `tests/unit/geometry/test_moments.cpp`, `tests/unit/autodigitize/test_autodigitize.cpp`
  (cas « Lot B », « Lot C » et « Lot D »), `tests/unit/optimization/test_order.cpp`,
  `tests/unit/geometry/test_boolean.cpp`.
