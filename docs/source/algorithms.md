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
  réinsérer dans les emplacements libres, verrous à leur place
coût = distance de déplacement + 50000 * changements de couleur
```

## Implémentation associée

Voir chaque chapitre thématique et `docs/stitch-engine-research.md` pour les
références (Douglas-Peucker, scanline fill, k-means, ré-échantillonnage par
longueur d'arc).
