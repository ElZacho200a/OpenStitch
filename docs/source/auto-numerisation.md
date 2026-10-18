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
| `border_ratio` | part du **pourtour** de l'image (0 à 1) occupée par cette couleur |
| `recommended` | fond clair : `lightness > min_lightness` (90) **et** `sides_touched >= min_sides_touched` (3) ; **ou** fond coloré : `sides_touched == 4` **et** `border_ratio >= min_border_ratio` (0,85) |

Les seuils sont dans `BackgroundCandidateOptions`. Le dialogue *Numérisation
automatique* coche la case seulement si `recommended`, et affiche la pastille
de couleur, le pourcentage de surface, L* et le nombre de bords touchés. Le
CLI (`openstitch-cli digitize`) applique la même règle avec
`--skip-background auto` (défaut ; anciennement `-1`) et imprime la candidate ;
`yes`/`no` (anciennement `1`/`0`) restent des choix explicites prioritaires. Sur la marine, le ciel (bleu, L* ≈ 43) n'est
plus ignoré.

**Fond coloré (2026-10-15).** Le critère « quasi blanc » laissait broder un papier ou un tissu
beige (L* ≈ 86) : sur une illustration de navire à fond crème, le fond entier était cousu (299 × 224 mm
et +22 % de points, contre 290 × 200 mm une fois le fond ignoré). Une couleur qui occupe au moins
85 % du pourtour **et** touche les quatre bords encadre le motif, quelle que soit sa clarté ; un
ciel ou une mer plein cadre ne remplit jamais tout le pourtour (le cas de test « image plein cadre »
reste non recommandé). `BackgroundCandidateOptions::min_border_ratio` règle le seuil ; une valeur
supérieure à 1 désactive cette voie.

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

## Nettoyage des îlots (fusion par couleur proche)

La quantification laisse de petits îlots collés à de grands aplats : des enclaves qui deviennent
chacune un objet (et au moins une coupe). Le nettoyage des îlots fait **une passe sur toute
l'image** : chaque région de moins de *A* mm² est fusionnée dans la voisine dont la **couleur est
la plus proche**, au sens CIELAB (ΔE76), et non dans celle qui partage la plus longue frontière
(règle de `min_region_area_mm2`). C'est l'équivalent « segmentation » de la fonction *boucher les
trous*.

- **Règle de choix** : voisine de ΔE minimal ; à égalité (1e-9), frontière commune la plus longue,
  puis plus petit identifiant. La région absorbante garde sa couleur. Les plus petites régions sont
  traitées d'abord, donc un amas d'îlots se résorbe de proche en proche. `max_delta_e` > 0 refuse
  une voisine plus éloignée (l'îlot reste tel quel) ; 0 = sans limite.
- **Lib** : `segmentation::merge_small_regions(seg, min_px, excluded, boundary_weight,
  MergeTarget::ClosestColour, max_delta_e)` et `cielab_delta_e(a, b)`.
- **CLI** : `digitize --island-cleanup MM2 [--island-max-delta-e N]` (`AutoOptions::island_area_mm2`,
  `island_max_delta_e`), appliqué avant `--min-region-mm2`. Désactivé par défaut.
- **Bureau** : menu *Segmentation → Nettoyer les petits îlots…* (aire maximale en mm², écart de
  couleur maximal). Un seul pas d'annulation (`commands::CleanIslandsCommand`, qui restaure les
  étiquettes réaffectées et les régions fusionnées). Si des objets vectoriels existent déjà pour des
  régions fusionnées, le même choix que pour *Fusionner* est proposé (conserver ou supprimer).

**Mesures** (illustration de navire 300 mm, 12 couleurs, fond ignoré ; coupes du DST sans
optimisation) :

| `--island-cleanup` | objets | coupes | surface non couverte |
|---|---|---|---|
| 0 (désactivé) | 344 | 887 | 8,0 % |
| 20 mm² | 173 | 681 | 6,9 % |
| 60 mm² | 113 | 592 | 6,0 % |

**Réserve.** Un trait fin sombre (mât, hauban) est un îlot comme un autre : à 20 mm², les haubans
du navire se fondent dans les voiles sombres. À associer à `--dark-lines`, qui sépare les traits
avant le nettoyage : 369 objets, 1 359 coupes, haubans lisibles (contre 1 499 coupes avec les
traits seuls).

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

**Réglage dans l'interface (2026-10)** : la boîte *Numérisation automatique* (mode formes) propose « Chevauchement des remplissages » (0 à 1 mm, par pas de 0,05 mm, 0 par défaut) ; la valeur est mémorisée entre les sessions (`autoDigitize/fillOverlapUm`) et alimente `AutoOptions::fill_overlap`. Même réserve que ci-dessus : le débord fait partie de la forme vectorielle.

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

### Brins diagonaux dans les remplissages (`--no-underpath`)

Vu au rendu réaliste (`project render --px-per-mm 36`) : les **liaisons entre morceaux de rangées**
d'un tatami (`hidden_underpath`, vrai par défaut) sont cousues PAR-DESSUS les rangées, en brins obliques
de la couleur du remplissage qui traversent la zone : c'est l'aspect « brouillon » des grandes zones.
`--no-underpath` (`AutoOptions::fill_hidden_underpath = false`) les remplace par des sauts et des coupes :
remplissages propres, au prix de coupes. Mesuré sur le navire (`--island-cleanup 0 --min-region-mm2 1
--dark-lines-bars 4 --dark-lines-min-length 6 --curve-tolerance 0.4`) :

| | points | sauts | coupes |
|---|---|---|---|
| liaisons (défaut) | 117 553 | 5 681 | 1 906 |
| `--no-underpath` | 116 574 | 10 687 | 3 222 |
| `--no-underpath --hidden-travel` | 113 172 | 10 293 | 2 653 |

`--hidden-travel` (ponts cousus sous des points cousus plus tard) récupère 569 coupes sans rien laisser
voir. Le vert intérieur (tissu visible, `scripts/green_holes.py`) ne bouge pas (1,52 % -> 1,59 %) : il
vient d'ailleurs. Écartés par la mesure : `--fill-overlap` (0,3 mm : 1,52 % -> 1,30 % pour +13 % de points
et des détails perdus 0,50 % -> 1,48 %), la compensation de tirage (1,43 % -> 0,98 % à 0,35 mm) et le
filtre des points courts (1,52 % -> 1,42 %). Test : `digitize applies fill inset, overlap and underpath
settings to created tatami`.

### Retrait du bord des tatami (`--fill-inset`)

Chaque tatami est rentré de `TatamiParams::inset` (0,2 mm) : deux zones jointives laissent ~0,4 mm
de vide, visible comme un filet clair autour de chaque zone. `AutoOptions::fill_inset` (CLI :
`--fill-inset MM`, défaut 0,2 inchangé, donc ni objets existants ni dorés modifiés) fixe ce retrait
pour les tatami créés. Mesure sur le navire (`--dark-lines --island-cleanup 20`,
`project check` -> `uncovered_ratio`, dont 0,324 de fond ignoré) :

| `--fill-inset` | points | coupes | non couvert |
|---|---|---|---|
| 0,2 (défaut) | 91 755 | 1 359 | 0,366 |
| 0,1 | 96 706 | 1 362 | 0,342 |
| 0 | 106 764 | 1 368 | 0,325 |

`inset` négatif refusé. Une compensation de tirage en plus (0,15 mm) n'apporte rien (0,321) et coûte
+14 % de coupes. Alternative à `fill_overlap` (désactivé, voir ci-dessus) : elle n'élargit pas la
surface vectorielle, donc pas de débord des autres types de points.

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
   `min_feature_width_mm` (**0 = désactivé par défaut** depuis le 2026-10 ;
   ancienne valeur 1,2 mm, convertie en pixels avec `mm_per_px`). Les
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
fins que le seuil (par exemple un mât fin), rendus à la région qui les entoure,
et arrondit les coins convexes.

**Pourquoi l'ouverture est désactivée par défaut (2026-10).** Sur un dessin au
trait, le réseau de contours est lui-même une forme fine : avec un noyau de
1,2 mm (8 px à 0,152 mm/px), la région 1 du projet `Homerde.osp` (contours
bleu marine, 271 939 px) passait d'une pièce à 97 trous (250 686 px
vectorisés) à **62 pièces, 2 trous et 166 604 px (-34 %)**. Le vecteur de
l'auto-numérisation différait donc de celui de la vectorisation manuelle
(`vectorize_region`, segmentation brute), qui est la référence. Seuil à 0, les
deux sont identiques (même nombre de pièces, de trous et de nœuds, même aire).
Le seuil reste réglable : `--min-feature-mm` (CLI), champ « Largeur minimale
des détails » (dialogue de numérisation automatique). Contrepartie : plus de
petites régions subsistent, que `min_region_area_mm2` fusionne ou transforme en
contour.

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

**Routage en chemin unique** (`ContourOptions::routing = SinglePath`,
`digitize --single-path`, défaut `Chains` = comportement historique inchangé).
L'assemblage historique (`assemble_chains`) n'apparie à une jonction que des
branches « alignées » (virage < 90°) : un éventail de branches serrées reste en
autant de polylignes, donc autant de sauts et de coupes. `route_single_path`
(`contour_route.cpp`) cherche à la place un **parcours eulérien** du réseau
(problème du postier chinois, version gloutonne déterministe) : 1) les
nœuds de degré impair sont les extrémités de polylignes inévitables (au minimum
`impairs / 2`) ; 2) deux impairs distants d'au moins `trim_threshold` (3 mm, au-delà
duquel un saut coûte une coupe) sont reliés en **repassant** sur le plus court
chemin du réseau s'il fait au plus `max_retrace` (12 mm) ; 3) les impairs
restants sont appariés au plus proche (liaison par saut) ; 4) circuit
d'Euler par Hierholzer, en continuant toujours par la branche la plus alignée,
coupé aux liaisons par saut. Un réseau sans nœud impair donne un seul circuit
fermé. Sur une grille de traits de test : 7 -> 5 polylignes et 7 -> 5 coupes.
Les idées viennent de la théorie des graphes ; l'outil libre Ink/Stitch, sous
GPL, n'a pas été consulté. Limites : la paire de repassage est choisie
gloutonnement (pas l'appariement optimal) ; les tronçons repassés sont cousus
deux fois (visible en point simple, absorbé en point triple).

**Lignes cohérentes** (photo -> traits) : appliquer d'abord l'opération
d'image `CoherentLinesOp` (voir [Traitement d'image](image-processing.md)), ou
`digitize --coherent-lines` ; le mode Contours reçoit alors un dessin au trait
net au lieu d'une photo.

**Métriques** (`ContourMetrics`) : composantes, segments, jonctions,
extrémités, polylignes (`trails`) et longueur repassée, branches courtes et éléments petits supprimés, longueur point
droit / satin (mm), largeur min / moyenne pondérée / max, replis, rejets.
Affichées par `openstitch-cli digitize --mode contours` et dans la barre
d'état du desktop.

**Utilisation** : CLI `digitize image.png out.dst --mode contours --detail
0.5 --technique auto|running|legacy-satin` (`satin` : ancien nom accepté ; défaut
`--mode shapes`, comportement inchangé ; voir [Ligne de commande](cli.md)) ; desktop : dialogue « Numérisation automatique » -> « Contours »,
curseur Détail 0-100 (défaut 50), technique Automatique / Running / Satin.
Pas d'aperçu superposé dans le dialogue pour l'instant.

**Limites connues** : un pincement en 8 n'est que partiellement satin (repli
point droit + avertissement) ; segments entre nœuds < 0,5 mm rejetés ; images
> 4000 px non testées ; groupes de couleur non ordonnés par proximité ; sur le
cas synthétique CLI, `detail` 0,9 produit des colonnes refusées sur un anneau
fin (« trou entre stations ») : le segment est alors découpé en plusieurs
objets au lieu de rester continu.

## Satin sur les bandes (`satin_bands`)

Par défaut l'auto-numérisation ne produit que du tatami (zones) ou du contour
(petites zones). `AutoOptions::satin_bands` (CLI : `--satin-bands` ; desktop : case « Satin automatique pour les bandes
étroites » du dialogue, mémorisée ; désactivé par défaut) rend le satin aux **bandes étroites** : anneaux, rubans, traits
épais, arcs.

Une région devient un **auto-satin par squelette** (`AutoSatinParams` par
défaut, éditable ensuite) si :

- elle fait au plus 1 trou (un anneau) ;
- sa largeur inscrite maximale est <= `satin_max_width` (6 mm) et sa largeur
  moyenne >= 0,8 mm (seuil de `SatinabilityThresholds`) ;
- elle est allongée : longueur >= 2,5 x largeur, avec longueur = périmètre
  total (extérieur + trous) / 2. Le rapport de satinabilité n'est pas utilisé
  ici : il ignore les trous dans le périmètre et un anneau n'a pas d'arête de
  squelette ;
- **la génération essayée** (`generate_skeleton_satin`, déterministe) couvre
  au moins `satin_min_coverage` (0,9) de la région.

Sinon : tatami, comme avant. Les **formes compactes sans trou** (disques,
pétales) de largeur <= `satin_compact_max_width` (12 mm, 0 = désactivé) sont
cousues en satin à cordes parallèles (`AutoSatinParams::compact_axis`, voir
`satin-squelette.md`), sous la même condition de couverture. Au-delà, tatami.
Essai CLI (anneau 2,4 mm, arc 3,7 mm, disque 11,6 mm) : les trois en satin,
7 sauts au lieu de 12 et plus de points courts de tatami.

**Limites** : la case du desktop n'a pas été compilée dans l'environnement de
développement Linux (Qt absent), à vérifier au prochain build Windows ; une région à plusieurs trous n'est pas tentée ; la couverture est
géométrique, pas validée sur machine ; pas de satin en éventail ou concentrique pour les grands disques
(voir `satin-squelette.md`).

## Couche de traits sombres (`--dark-lines`, HP-AUTO-005)

Sur une illustration à contours noirs (mâts, haubans, bords de nuages), le mode formes seul traite
un trait comme n'importe quelle région : il fusionne avec les aplats sombres voisins et devient un
amas de blocs. `auto_digitize_with_lines` (`line_layer.cpp`) sépare les traits :

1. **Détection sur l'image**, pas sur les régions quantifiées : `segmentation::dark_thin_mask`
   garde les pixels opaques de clarté L* <= `dark_max_lightness` (25) qui appartiennent à une partie
   plus étroite que `max_line_width_mm` (1,6 mm) de l'ensemble des pixels sombres (ouverture
   morphologique). Un trait fin anti-crénelé est découpé et mélangé aux voisines par la
   quantification ; sur l'image il reste continu.
2. **Remplissages** : ces pixels sont rendus aux régions voisines (`reassign_to_neighbours`, vote
   4-connexe), les aplats que le trait coupait en deux sont refusionnés (même couleur, redevenus
   adjacents), et une région presque entièrement faite de pixels de trait quitte les remplissages.
   Les remplissages passent donc sous le trait, sans trou.
3. **Traits** : une segmentation à une seule couleur passe par
   `analyze_contours` et `build_contour_objects` : lignes médianes en point droit, parcours
   eulérien, mêmes garde-fous que le mode contours. Les objets de ligne sont ajoutés **après** tous
   les remplissages : ils sont cousus en dernier.
   **Couleur du fil** : la moyenne des pixels de trait tombe presque toujours à quelques unités
   d'un noir déjà présent dans la palette (navire : `#0C121C` contre `#08080F`), ce qui ajoutait un
   fil pour rien. Elle est donc **calée sur la couleur de région la plus proche** quand la distance
   RGB est <= 30, et les remplissages de cette couleur sont placés en dernier (avant les traits, ordre
   relatif conservé) pour que le noir ne soit cousu qu'une fois. Mesuré sur le navire : 8 blocs et 7
   changements de fil -> 7 blocs et 6, points et coupes identiques. Tests :
   `couche de traits : la couleur des traits reprend le noir de la palette`.

**Dans le bureau** : la boîte *Numérisation automatique* (mode formes) propose « Traits sombres
cousus par-dessus » (largeur maximale en mm et clarté L\* maximale, mémorisées entre les sessions) et
« Nettoyage des îlots » (mm²). L'image lue est l'image de travail (opérations appliquées) quand elle a
les dimensions de la segmentation, sinon l'image d'origine. Même routage que le CLI : point droit,
parcours eulérien.

**Correctif de la détection des séparations (2026-10-16).** `digitize --debug-dir` (image
`traits.png`) a montré que presque tous les cordages d'un navire étaient classés « séparation » : les
pixels anti-crénelés d'un trait sont étiquetés dans la région NOIRE par la quantification, donc un
hauban sur le ciel voyait « ciel + noir » (deux couleurs) et était écarté. Les régions dont la clarté
est celle des traits ne comptent plus comme zone voisine. Test (échoue sans le correctif :
`cousus=0 separation=240`) : `objects_only garde un hauban dont le halo est dans la region noire`.
Avec `--dark-lines-objects-only`, utiliser une longueur minimale plus faible (3 mm) : les cordages
coupés aux frontières de voile donnent des fragments courts.

**Séparation ou vrai objet (`--dark-lines-objects-only`).** Une illustration à contours noirs a deux
sortes de traits sombres : ceux qui **séparent deux zones** (contour d'un nuage entre ciel et nuage,
bord d'une voile) et ceux qui sont de **vrais objets** (hauban, mât, cordage sur une zone unie).
Coudre les premiers par-dessus double la frontière et salit les voiles. Le critère se lit dans la
segmentation : autour d'un pixel de trait (fenêtre de rayon largeur/2 + 1 px, pixels de trait
exclus), au moins **deux régions de couleurs différentes** -> séparation, non cousue (le pixel reste
rendu aux zones voisines, qui se rejoignent) ; une seule couleur -> objet, cousu. Le fond ignoré
(label 0) ne compte pas : le contour du motif contre le fond reste un trait. Une fermeture de 2 px
(limitée aux pixels de trait) comble les petites coupures d'un cordage qui traverse une frontière.
Mesuré sur le navire (`--island-cleanup 0 --min-region-mm2 1`) : objets de ligne 327 -> 135, coupes
1 989 -> 1 753 (-12 %), points 121 698 -> 116 087 ; la fidélité mesurée baisse (détails perdus 0,76 % ->
2,31 %) puisque des traits présents dans la source ne sont plus cousus : c'est le prix, voulu, d'un
rendu plus propre. Quelques haubans fins entre voile et ciel sont perdus. Test :
`couche de traits : objects_only ne coud pas les separations entre deux zones`. Option du CLI
seulement (pas encore dans la boîte de dialogue du bureau).

**Barres épaisses (`--dark-lines-bars MM`).** Entre le trait fin (<= 1,6 mm, point droit) et le
remplissage il manquait un niveau : le mât et les vergues (2 à 3 mm) sont plus larges qu'un trait,
donc restaient dans le remplissage noir, où le réseau mât + vergues + coque en un seul bloc ressortait
**hachuré et déchiqueté** (franges retirées, cœur en tatami). Avec cette option, les parties sombres
plus étroites que MM (essayer 4) mais plus larges qu'un trait sont retirées des remplissages (rendues
aux zones voisines), regroupées en une région de la couleur des traits et cousues comme **bandes**
(`satin_bands` : auto-satin par squelette si la couverture est >= 90 %, sinon tatami), AVANT les
traits fins, sur le même fil. Le seuil de clarté des barres est **serré** (L* du trait + 8, soit ~12) :
au seuil large (L* <= 25) le bleu marine (L* ~ 16) passait pour une barre et ses parties étroites
étaient recolorées en noir (réseau géant de 1 963 mm2 sur toute l'image, détails perdus 2,3 % -> 5,5 %).
Les pixels fins à ce seuil serré sont exclus des barres. Mesuré sur le navire
(`--island-cleanup 0 --min-region-mm2 1`) : détails perdus 0,76 % -> 0,79 % (inchangé), coupes
1 989 -> 2 076 (+4 %), objets de ligne conservés ; mât et vergues passent d'un amas hachuré à des barres
pleines. Test : `couche de traits : bars_max_width_mm coud les barres epaisses a part`.
Hypothèse ÉCARTÉE par la mesure : détecter aussi les traits fins au seuil serré (contours noirs collés
à un aplat marine) ajoutait 21 % de fil noir et 17 % de coupes sans gain de fidélité (0,76 % -> 0,81 %).

**Seuil de clarté des traits (correctif, 2026-10-16).** Le seuil `--dark-lines-lightness` (25) n'est
plus qu'un plafond : le seuil effectif est la clarté de la couleur la plus SOMBRE réellement présente
dans la segmentation (régions d'au moins 0,05 % des pixels) plus 8, plafonné par `--dark-lines-lightness`.
Avant, L* <= 25 englobait le bleu marine (`#022655`, L* ~ 16) : les parties étroites des vagues
marine passaient pour des traits, étaient retirées du remplissage marine (**trou** : seul un trait fin
les recouvrait, le tissu apparaissait) puis cousues en noir (traits noirs plats posés dans la mer) ;
les petites zones recolorées en taches noires sur la coque en venaient aussi. Mesuré sur le navire
(`--island-cleanup 0 --min-region-mm2 1 --dark-lines-bars 4`) : détails perdus 0,79 % -> 0,35 %, trous
intérieurs 522 -> 330 px (24 -> 18 trous, tous <= 1,5 mm2), fil noir posé hors zone sombre 15 % -> 12 %,
coupes 2 076 -> 2 319 (+12 %). Une image dont les traits ne sont pas noirs (traits marine) garde un
seuil plus haut (son pixel le plus sombre le fixe).

**Fragments isolés (`--dark-lines-min-length MM`).** Les composantes de trait dont la diagonale de
boîte englobante est inférieure à MM ne sont pas cousues (éclats de vagues redressés en tirets plats :
un objet et une coupe chacun). Navire : 6 mm retire 37 objets (-1,6 % de coupes, détails perdus +0,07
point) ; 10 mm retire 66 objets (+0,18 point). Test : `min_line_length_mm ecarte les fragments isoles`.

Limites : un seul fil pour tous les traits ; point droit seulement (pas de satin à largeur
constante) ; un trait contre un aplat sombre de même teinte n'est pas détecté (il serait invisible).
Chaque trait étant un tracé séparé, la couche ajoute des coupes (+486 sur l'illustration de test) ;
`--detail` plus bas en réduit le nombre.

## Stippling (semis de points relié en un trait)

`libs/stipple` (Qt-free, sans dépendance GPL) transforme une image en un
**unique trait de points droits** dont la densité reproduit les tons.

1. **Densité** : luminance Rec. 601 → `(1 - L)^gamma` (alpha < 128 = vide),
   moyenne par blocs jusqu'à 600 px max (`--stipple-invert` : zones claires
   denses, pour un fil clair sur fond sombre).
2. **Semis** (Secord, « Weighted Voronoi Stippling », NPAR 2002) : tirage par
   rejet selon la densité (générateur xorshift64* portable, graine fixe), puis
   relaxations de Lloyd où chaque point va au centroïde **pondéré par la
   densité** de sa cellule de Voronoï (cellules exactes sur la grille). On
   obtient un bruit bleu, dense dans les zones sombres.
3. **Tour** : ordre de Hilbert, puis 2-opt (listes de voisins, bits
   « don't look ») et Or-opt. Concorde n'est volontairement pas utilisé
   (licence académique). Le tour fermé est coupé à son arête la plus longue
   et aux liaisons > 8 mm : aucune liaison ne traverse un grand vide.
4. **Objets** : un `VectorObject` (chemins ouverts) + un objet brodé point
   droit (longueur 1,5 mm), fil noir par défaut.

**Utilisation** : `openstitch-cli digitize image.png out.dst --mode stipple
[--stipple-points N] [--stipple-seed S] [--stipple-invert]` (défaut 2000
points). Résultat déterministe pour une graine donnée.

**Limites connues** : pas de couleur (une seule couche monochrome) ; pas de
bouton dans le desktop pour l'instant ; le trait reste un tour de points, pas
une trame de lignes de densité variable (PhotoStitch complet = HP-STI-017
partiel) ; un tour TSP heuristique est ~5 % au-dessus de l'optimum.

## Bloc uni (`--solid-block`)

**Besoin** : une broderie pleine d'un seul tenant, où seules les couleurs changent d'une zone à
l'autre, sans jamais voir le tissu entre deux zones voisines.

**Cause des interstices** : chaque tatami est rentré de `inset` (0,2 mm) sur tous ses bords et
`fill_overlap` vaut 0 par défaut (voir Lot C : le débord, porté par l'objet vectoriel, faisait
déborder les autres types de points créés ensuite). Deux zones voisines laissent donc une bande de
tissu. Une région sous `min_fill_area_mm2` (4 mm²) devient en plus un simple contour, ce qui crée des
trous.

**Réglage** : `autodigitize::apply_solid_block(options, overlap_mm = 0,5)`.

- `fill_overlap` passe à au moins `overlap_mm` : débord sous les voisins BRODÉS uniquement. Le bord
  extérieur du motif et le contact avec le fond ignoré gardent le retrait.
- `min_region_area_mm2` passe à au moins `min_fill_area_mm2` : une petite région est fusionnée dans sa
  voisine (aucun trou) au lieu d'être cousue en contour.
- Jamais de réglage à la baisse : une valeur déjà plus élevée est conservée. Désactivé par défaut,
  sortie inchangée sans l'option.
- **CLI** : `digitize --solid-block [--solid-block-overlap MM]` (`DigitizeSettings::solid_block`,
  `solid_block_overlap_mm`). **Bureau** : case *Bloc uni (sans tissu visible)* du dialogue de
  numérisation (mode formes).

**Mesures** (illustration de voilier 321 mm, `--max-colors 12 --dark-lines`, binaire Debug) :

| Réglage | points | sauts | points < 0,5 mm | surface non couverte |
|---|---|---|---|---|
| sans option | 130 189 | 1 902 | 1,1 % | 6,19 % |
| `--solid-block-overlap 0` (fusion seule) | 129 590 | 1 843 | 1,0 % | 6,14 % |
| 0,15 mm | 213 309 | 25 703 | 20,6 % | 2,24 % |
| 0,3 mm | 227 863 | 8 473 | 5,9 % | 0,67 % |
| **0,5 mm (défaut)** | 165 355 | 1 102 | 3,3 % | 0,29 % |
| 0,8 mm | 169 376 | 960 | 3,1 % | 0,22 % |

Un débord inférieur à environ un pixel de l'image (0,3 mm ici) fragmente les remplissages (lamelles,
rangées très courtes, sauts) : d'où le défaut de 0,5 mm. La fusion des petites régions est neutre ;
tout le gain vient du débord.

**Limite connue** : le débord reste porté par l'objet vectoriel (conception du Lot C) ; un satin ou un
contour créé ensuite sur une région ainsi élargie déborde aussi. Rendu visuel non vérifié (pas de
rendu raster en ligne de commande) : contrôle à faire dans le bureau.

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
