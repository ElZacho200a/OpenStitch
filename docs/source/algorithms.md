# Algorithmes

Public : développeur. Ce chapitre décrit les algorithmes **réellement** utilisés,
avec un pseudo-code reflétant l'implémentation (et non une copie du C++).

## Quantification (segmentation)

- **Objectif** : réduire l'image à N couleurs perceptuellement cohérentes.
- **Principe** : conversion RGB→CIELAB, k-means déterministe sur un échantillon,
  affectation au centre le plus proche.
- **Complexité** : ~O(pixels · N) pour l'affectation.
- **Cas limites** : image entièrement transparente (refus), N hors bornes.

```
segment(image, N):
  opaques = pixels d'alpha >= 128 en Lab
  centres = kmeans(échantillon(opaques), N, graine fixe)
  pour chaque pixel opaque: label = argmin_c ||Lab(pixel) - centres[c]||
  régions = composantes_connexes(label)   # 4-connexité, par couleur
  absorber les régions < taille_min dans la voisine majoritaire
```

## Extraction et simplification de contours

- **Contours** : Suzuki-Abe (`cv::findContours`, RETR_CCOMP).
- **Simplification** : Douglas-Peucker, tolérance **adaptative** (≤ périmètre/16)
  pour préserver les petits trous.
- **Nettoyage** : union Clipper2 (règle pair-impair) → hiérarchie extérieur/trous.

## Aplatissement de Bézier + longueur d'arc

```
flatten(path, tol):
  pour chaque segment à tangentes: subdiviser (De Casteljau) tant que
      distance(contrôles, corde) > tol
  fusionner les points identiques

resample_run(points, cible):
  L = longueur(points)
  n = max(1, ceil(L / cible))         # parts égales, <= cible, sans résidu
  renvoyer points aux abscisses i*L/n, i=0..n
```

## Lissage, conversion en courbes, formes et mesure (libs/geometry)

Fonctions pures, Qt-free, en micromètres entiers, déterministes (HP-VEC-007,
008, 010, 014). Le câblage IHM/commandes reste à faire (voir la roadmap).

- **`smooth_path(path, tol)`** : `flatten_to_polygon(path, tol)` puis
  `fit_curves` (Schneider) avec `tolerance = tol`. Les extrémités d'un tracé
  ouvert sont réimposées à leurs positions d'origine. Moins de 4 nœuds ou
  `tol <= 0` : tracé rendu inchangé. `smooth_path_set` traite extérieur et trous.
- **`convert_to_curves(path, opts)`** : pour chaque sommet sans tangente dont
  l'angle de braquage `atan2(|cross|, dot)` est ≤ `corner_angle_deg` (défaut
  60°), tangentes colinéaires dirigées selon la corde `précédent → suivant`,
  de longueur `handle_ratio` (défaut 1/3, borné à 1/2) fois le segment adjacent.
  Contrairement à `smooth_path`, les sommets ne bougent pas : la courbe passe
  par tous. Extrémités d'un tracé ouvert, coins francs, nœuds déjà courbes et
  voisins confondus restent inchangés.
- **`reshape_segment(path, seg, t, delta)`** (`reshape.hpp`, HP-VEC-016) : tire le
  point `t` du segment `seg` de `delta` sans bouger les nœuds : poignées
  `P1' = P1 + δ·w1/N`, `P2' = P2 + δ·w2/N` avec `w1 = 1-t`, `w2 = t`,
  `N = b1·w1 + b2·w2` (Bernstein `b1 = 3(1-t)²t`, `b2 = 3(1-t)t²`), `t` borné à
  [0,15 ; 0,85]. `nearest_segment` retrouve segment et `t` (échantillonnage puis
  recherche ternaire), `reshape_path_set_segment` refuse un résultat qui introduit un
  croisement (`path_set_has_crossings`).
- **`transform_nodes(path, selected, T, rayon)`** (`node_transform.hpp`, HP-VEC-017) :
  `p' = pivot + scale·R(θ)·(p − pivot) + translation` pour les nœuds sélectionnés
  (tangentes : partie échelle/rotation seulement). Transition douce : chaque nœud non
  sélectionné à `d < rayon` de son plus proche nœud sélectionné (égalité : plus petit
  indice) suit son déplacement pondéré par `w = 3u² − 2u³`, `u = 1 − d/rayon`.
  `transform_path_set_nodes` refuse un croisement que l'original n'avait pas.
- **`apply_brush_dab(path, dab)`** (`brush.hpp`, HP-VEC-018) : (1) subdivision exacte
  des segments de la zone plus longs que `max(R/3 ; 300 µm)` (budget 64 nœuds) ;
  (2) Push : `p += delta·w(d)` ; Smooth : `p += (milieu(voisins) − p)·w(d)`,
  `w(d) = force·(3u² − 2u³)`, `u = 1 − d/R` ; (3) retrait des nœuds droits de la zone
  à moins de 30 µm de la corde de leurs voisins. Tangentes inchangées, extrémités
  ouvertes fixes. `brush_result_is_valid` refuse un croisement nouveau.
- **Formes** (`shapes.hpp`) : étoile (2n coins), cœur (6 nœuds, 2 coins, Béziers
  dans le cadre), flèche (7 coins), rectangle arrondi (8 nœuds, quarts de cercle
  de constante κ = 0,5523, rayon borné à la moitié du petit côté), arc (un segment
  par quart de tour, poignée `4/3·tan(θ/4)·r`) et spirale d'Archimède
  (12 échantillons par tour, spline de Catmull-Rom via `smooth_open_path`).
  Entrée dégénérée : chemin vide.
- **Mesure** (`measure.hpp`) : distance et direction `atan2(dy, dx)`,
  longueur de chemin sur le tracé aplati (fermeture incluse si fermé), angle
  au sommet `atan2(|cross|, dot)` dans [0, π], aire par la formule des lacets
  (valeur absolue), conversions mm / mm² à l'affichage seulement.

## Running stitch

```
run_stitch(path, cfg):
  poly = flatten(path, cfg.tol)
  coins = sommets d'angle de rotation > seuil (+ extrémités si ouvert)
  pour chaque tronçon entre coins: concat(resample_run(tronçon, cible))
```
- **Complexité** : O(points).
- **Cas limites** : chemin vide/trop court → avertissement, pas de crash.

## Tatami (scanline + routage)

```
fill_tatami(région, params):
  tourner la géométrie de -angle
  pour chaque rangée y: segments = intervalles intérieurs (pair-impair)
  graphe: segments de rangées voisines qui se chevauchent en x sont adjacents
  parcours glouton: suivre une arête (couture) ; sinon sauter (déplacement)
  retourner dans le repère d'origine
```
- **Garantie** : aucune couture ne traverse un trou (adjacence ⇒ bande intérieure).
- **Complexité** : ~O(segments²) au pire pour l'adjacence locale par rangée
  (voisines uniquement), acceptable aux tailles usuelles.

## Satin

L'auto-satin (squelette, traversées orientées, pas adaptatif) est décrit dans
`satin-squelette.md` ; le pseudo-code ci-dessous ne concerne que le satin manuel à deux rails.

```
fill_satin(railA, railB, cfg):
  pour u de 0 à 1 par pas dérivé de la densité:
    L = point(railA, u*longueurA) ; R = point(railB, u*longueurB)
    appliquer compensation (écarter L et R le long de LR)
    émettre zigzag alterné L,R
  sous-couche centrale = point droit sur (L+R)/2
```

## Encodage / décodage DST

```
encode(seq):
  pour chaque commande: delta = round(pos/100) - round(prev/100)
      subdiviser si |delta| > 121 (sauts)
      émettre 3 octets (ternaire équilibré + bits de type)
  en-tête calculé après le corps
```
- **Propriété** : déterministe, erreur ≤ 50 µm, sans dérive.

## Ordre de couture

```
optimize_order(items, stratégie):
  items libres (non verrouillés) réarrangés:
    ByColor: regrouper par couleur (ordre d'apparition)
    ByProximity: plus proche voisin
    ColorThenProximity: groupes de couleur puis proximité
    LayeredColorThenProximity: groupes de couleur triés par aire max
      décroissante ; dans chaque groupe, grandes zones (>= 25 % de la plus
      grande) par aire décroissante, puis petites par proximité
  réinsérer dans les emplacements libres, verrous à leur place
coût = distance de déplacement + 50000 * changements de couleur
```

## Implémentation associée

Voir chaque chapitre thématique et `docs/stitch-engine-research.md` pour les
références (Douglas-Peucker, scanline fill, k-means, ré-échantillonnage par
longueur d'arc).
