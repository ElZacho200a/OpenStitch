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
**Désactivé par défaut (2026-10).** `fill_overlap` vaut maintenant 0 : la surface élargie
était portée par l'objet vectoriel lui-même, donc tout autre type de points créé ensuite sur la
région (satin, contour cousu, remplissage directionnel) débordait de sa zone (+14 à +57 %
d'aire mesurés sur un projet réel). Les contours restent ceux de la segmentation, et chaque
tatami garde son retrait `inset` (0,2 mm). Conséquence : un interstice réapparaît entre deux
couleurs voisines ; une valeur de `fill_overlap` > 0 réactive la correction ci-dessus à la
demande. Dans le bureau, créer un satin ou passer un objet en satin ramène en plus un
contour agrandi par une ancienne auto-numérisation à celui de sa région (un seul pas
d'annulation).

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

**Aire vectorisée.** Le seuil porte sur l'aire que la région aura une fois
vectorisée, pas sur son nombre de pixels. Le contour passe par les centres
des pixels de bord : une tache de N pixels et B arêtes de frontière donne un
polygone d'aire N − B/2 + 1 (théorème de Pick). Sans cette correction, des
régions de 3 mm² en pixels devenaient des contours de 2,5 mm² sur la
marine. `merge_small_regions` reçoit donc un poids de frontière de ½
(`boundary_weight`).

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
| `trim_threshold` | 3 mm | au-delà : arrêt de sortie, coupe, déplacement, arrêt d'entrée ; en deçà : simple saut dans un même objet ; entre deux objets, la coupe est toujours faite |
| `trim_before_color_change` | oui | coupe avant chaque changement de fil |
| `lock_type` | aller-retour | point d'arrêt à l'entrée/sortie de chaque objet et autour de chaque coupe |
| `lock_length`, `lock_passes` | 0,8 mm, 2 | taille (bornée au point voisin) et répétitions |

Réglables dans le desktop (menu *Broderie ▸ Options de génération…*,
annulable) et dans le CLI (`digitize --trim-threshold <mm> --lock
none|backforth|triangle|zigzag`). Un projet `.osp` antérieur est relu sans
finitions (séquence identique).

## Déplacements internes et points courts (Lot F)

**Trajets cachés.** Les tatami créés par l'auto-numérisation reçoivent
`hidden_underpath = true` (`AutoOptions::fill_hidden_underpath`) : la
liaison entre deux composantes de rangées devient un trajet cousu caché sous
la couche supérieure (direct s'il reste court et intérieur, sinon le long du
contour rentré ; voir *Remplissage tatami*) au lieu d'un saut. Le défaut de
`TatamiParams` reste `false`.

**Points courts.** Dernier réglage de `project.finishing` :
`filter_short_stitches` (activé) et `min_stitch_length` (0,5 mm). Avant la
pose des verrous, `finish_sequence` retire chaque point cousu plus proche que
ce seuil du point conservé précédent, ce qui revient à le fusionner avec le
suivant. Aucun point n'est déplacé. Ne sont jamais retirés :

- le premier et le dernier point d'un tracé ;
- les passes de verrou (`Lock`), qui sont d'ailleurs ajoutées après le filtre ;
- les points retouchés à la main (`Manual`) ;
- un point dont le retrait ferait passer le nouveau segment hors de la région
  de son objet (`segment_stays_in_region` sur l'objet vectoriel suivi) — par
  exemple le point d'un angle intérieur.

La piqûre de longueur nulle à l'arrivée d'un saut (`Jump p0` puis `Stitch
p0`) est omise quand un verrou d'entrée suit : le verrou revient piquer en
`p0`.

## Mesures et critères d'acceptation (Lot G)

`openstitch-cli stats fichier.dst` imprime, en plus des compteurs de base,
les mesures de `stitch_analysis::sequence_metrics` (voir *Analyse et
validation*) : déplacements, dont ceux de plus de 3 mm sans coupe, points de
moins de 0,5 mm hors points d'arrêt, histogramme des directions.
`openstitch-cli digitize` imprime en plus `project_metrics` : objets de moins
de 3 mm², surface non couverte hors fond ignoré, angles de remplissage
distincts, ventilation par source.

## Contours / Line Art

Seconde stratégie de l'auto-numérisation, pour les **dessins au trait** :
au lieu de remplir les régions, on coud les *traits*. Entrée : la même
segmentation CIELAB ; sortie : des objets éditables (`AutoResult`), jamais
des points stockés.

**Pipeline** (`libs/autodigitize/src/contour_*.cpp`) : segmentation → pour
chaque couleur, squelette / lignes médianes avec largeur locale → réseau de
nœuds (jonctions, extrémités) et segments (`ContourNetwork`) → nettoyage
selon `detail` (branches courtes, boucles et éléments isolés trop petits,
fusion de traits proches, simplification Douglas-Peucker) → classification
par segment (`classify_segment` : point droit simple, point triple, satin,
rejeté) → objets : une couleur = un groupe contigu (les plus claires d'abord,
la plus sombre en dernier), ordre déterministe. Les lignes sont des objets
Running à chemin ouvert ; l'auto-numérisation ne produit pas de satin
(`satin_planning` a été supprimé) : l'utilisateur convertit ensuite en satin automatique.

**Détail → seuils** (`contour_thresholds`, fonction pure et monotone ; facteur
`f = 4^(1 - 2*detail)`, 0,5 = réglages historiques) :

| detail | f | branche min | élément isolé min | tolérance DP | fusion |
|---|---|---|---|---|---|
| 0,0 | 4 | 6 mm | 9,6 mm | 0,40 mm | 1,2 mm |
| 0,25 | 2 | 3 mm | 4,8 mm | 0,28 mm | 0,6 mm |
| 0,5 | 1 | 1,5 mm | 2,4 mm | 0,20 mm | 0,3 mm |
| 0,75 | 0,5 | 0,75 mm | 1,2 mm | 0,14 mm | 0,15 mm |
| 1,0 | 0,25 | 0,375 mm | 0,6 mm | 0,10 mm | 0,075 mm |

Boucle fermée minimale : périmètre π × branche min. Chaque seuil est borné par
les garde-fous physiques ci-dessous.

**Garde-fous physiques** (`contour_limits`, jamais abaissés par `detail`) :
largeur satin min = `SatinabilityThresholds::min_satin_width` (0,8 mm) et max
= `AutoOptions::satin_max_width` ; longueur min d'élément =
`RunningStitchParams::min_length` ; périmètre de boucle min = 3 fois cette
longueur ; tolérance de simplification min = 0,1 mm (pas DST). Heuristiques de
qualité satin (variation de largeur 50 %, virage 60° sur ~1 mm) ignorées quand
la technique est forcée à Satin. Un satin voulu mais impossible retombe en
point droit avec un avertissement (jamais silencieux) ; les segments
inférieurs à 0,5 mm entre nœuds sont rejetés avec avertissement.

**Technique** : Automatique (par segment : fin -> point droit, régulier ->
satin, sinon repli), Running (point droit sur la ligne médiane), Satin (satin
quand la largeur le permet).

**Jonctions** : croisements (X, T) conservés comme nœuds du réseau ; les
colonnes satin aboutissant à une jonction étaient ancrées par l'ancien `satin_planning`
(supprimé) ; le satin par squelette traite les jonctions par cellules (voir `satin-squelette.md`).

**Métriques** (`ContourMetrics`) : composantes, segments, jonctions,
extrémités, branches courtes et éléments petits supprimés, longueur point
droit / satin (mm), largeur min / moyenne pondérée / max, replis, rejets.
Affichées par `openstitch-cli digitize --mode contours` et dans la barre
d'état du desktop.

**Utilisation** : CLI `digitize image.png out.dst --mode contours --detail
0.5 --technique auto|running|satin` (défaut `--mode shapes`, comportement
inchangé) ; desktop : dialogue « Numérisation automatique » -> « Contours »,
curseur Détail 0-100 (défaut 50), technique Automatique / Running / Satin.
Pas d'aperçu superposé dans le dialogue pour l'instant.

**Limites connues** : un pincement en 8 n'est que partiellement satin (repli
point droit + avertissement) ; segments entre nœuds < 0,5 mm rejetés ; images
> 4000 px non testées ; groupes de couleur non ordonnés par proximité ; sur le
cas synthétique CLI, `detail` 0,9 produit des colonnes refusées sur un anneau
fin (« trou entre stations ») : le segment est alors découpé en plusieurs
objets au lieu de rester continu.

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
- `libs/autodigitize/src/contour_*.cpp` — stratégie Contours
  (`auto_digitize_contours`, `contour_thresholds`, `contour_limits`).
- `apps/cli/main.cpp` — `run_digitize` (`--mode contours`).
- Tests : `tests/unit/segmentation/test_segmentation.cpp` (cas « fond
  blanc », « plein cadre coloré », « blanc central », « deux bords »),
  `tests/unit/desktop/test_main_window.cpp`
  (`autoDigitizeDialogDoesNotSkipColoredFullFrameRegion`,
  `autoDigitizeDialogSkipsNearWhiteFramingBackground`),
  `tests/unit/geometry/test_moments.cpp`, `tests/unit/autodigitize/test_autodigitize.cpp`
  (cas « Lot B », « Lot C » et « Lot D »), `tests/unit/optimization/test_order.cpp`,
  `tests/unit/geometry/test_boolean.cpp`.
