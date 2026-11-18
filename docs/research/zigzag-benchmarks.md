# RND-ZIGZAG-01 — L1 : banc comparatif ZigZag / OpenStitch

Statut : **premier banc, étape mixte image → DST**. Ce n'est pas encore une comparaison du seul
moteur de points ; aucune conclusion de qualité n'en découle.

## Références épinglées (16/11/2026)

- OpenStitch `main` : `3fade8c7cff1006969d4f042b1d303e0fb301a5a`, `openstitch-cli` 0.1.0, build msvc Release local.
- ZigZag `main` : `429a028054145fc3e541525bb05b57596fa49d22`, 0.1.5, Apache-2.0, compilé depuis
  ce SHA (`cargo build --release --locked --bin zigzag`, Rust 1.96, sans GUI). Aucun binaire téléchargé.

## Relancer

```bash
python scripts/bench/zigzag/make_fixtures.py <dossier>/fix
python scripts/bench/zigzag/run_bench.py --zigzag <zigzag.exe> \
  --openstitch build/msvc/apps/cli/Release/openstitch-cli.exe \
  --fixtures <dossier>/fix --work <dossier>/work --out bench-results.json
```

1 warmup + 5 répétitions, temps mur (démarrage du processus inclus), pic RSS par sondage,
timeout 120 s. Les deux DST sont décodés par le **même** décodeur (`openstitch-cli stats --json`).
Le JSON brut n'est pas versionné (régénérable).

## Normalisation et protocole

- Entrée identique : PNG synthétiques à fond blanc (6 motifs, 400 px).
- Échelle : ZigZag convertit un PNG à 0,2 mm/px (mesuré : cercle de 320 px → 64,2 mm) ;
  OpenStitch reçoit `--dpi 127` pour la même échelle. Autres réglages : valeurs par défaut des deux
  outils, **non normalisées** (densité, sous-couche, finitions, nombre de couleurs diffèrent).
- Les boîtes englobantes diffèrent encore de quelques mm (ex. `big_concave` 72 contre 80 mm) :
  les deux outils ne produisent pas la même géométrie, donc pas les mêmes points.

## Résultats (médiane de 5 répétitions)

| motif | outil | p50 s | RSS Mo | points | coupes | sauts | couleurs | fil m | pts < 0,5 mm |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| big_concave | zigzag | 0,181 | 107 | 7896 | 0 | 13 | 1 | 20,7 | 478 |
| big_concave | openstitch | 0,131 | 28 | 8550 | 7 | 26 | 2 | 20,9 | 259 |
| logo_4c | zigzag | 0,313 | 112 | 7319 | 0 | 20 | 4 | 15,9 | 825 |
| logo_4c | openstitch | 0,126 | 31 | 4650 | 4 | 8 | 3 | 10,9 | 290 |
| many_small | zigzag | 0,333 | 105 | 14060 | 0 | 2968 | 1 | 9,5 | 3355 |
| many_small | openstitch | 0,131 | 28 | 17280 | 540 | 665 | 1 | 10,1 | 0 |
| narrow_spike | zigzag | 0,178 | 109 | 1423 | 0 | 14 | 1 | 2,6 | 242 |
| narrow_spike | openstitch | 0,125 | 30 | 890 | 1 | 1 | 1 | 1,9 | 152 |
| peto_text | zigzag | 0,177 | 107 | 2265 | 0 | 25 | 1 | 3,7 | 409 |
| peto_text | openstitch | 0,125 | 27 | 1440 | 4 | 13 | 1 | 2,7 | 143 |
| u_ring_crescent | zigzag | 0,319 | 112 | 3072 | 0 | 19 | 3 | 5,2 | 584 |
| u_ring_crescent | openstitch | 0,126 | 28 | 1707 | 4 | 12 | 3 | 3,7 | 121 |

## Lecture prudente

- Temps : tous les cas durent moins de 0,4 s, dominés par le démarrage ; l'écart (0,13 contre
  0,18-0,33 s) ne dit rien du moteur de points ni du comportement sur 100k-250k points.
- ZigZag n'émet aucune coupe : le décodeur compte les coupes DST, ZigZag les remplace peut-être
  par des sauts. À vérifier avant toute comparaison « coupes ».
- ZigZag produit plus de points courts (< 0,5 mm) sur les motifs ci-dessus ; non analysé
  (sous-couche, finitions et verrous différents ?).
- `many_small` : 2968 sauts contre 665 côté OpenStitch, à relier à la politique de coupe/saut.

## Limites et suite

- Pas de temps par phase, pas de défauts de couverture ni de rendu normalisé, pas de Linux,
  pas d'essai textile.
- La voie « même géométrie » (SVG → points) existe : voir la section suivante (`import_svg` côté OpenStitch).
- Cas 100k/250k points, 1000 régions réelles, recouvrements, re-numérisation à chaud : non faits.

## Voie « même géométrie » (SVG vectoriel → points)

Statut : **premier passage, 14 cas (8 formes en remplissage, 6 en satin)**, 5 répétitions + 1 warmup.
C'est ici que le moteur de points est isolé de la segmentation et du traçage.

### Protocole

- Entrée : mêmes SVG en mm (`scripts/bench/zigzag/make_svg_fixtures.py`), un seul chemin par fichier.
- ZigZag : `zigzag convert x.svg -o x.dst`, mode et réglages portés par l'id du chemin
  (`fill a=45 s=0.4 l=3 u=0 pc=0` ; `satin s=0.4 u=0 pc=0 ss=0`).
- OpenStitch : `project new` + `import_svg` (+ `auto_satin spacing_mm=0.4`) + `set_params`
  (angle 45°, espacement 0,4 mm, point 3 mm, sous-couche et compensation de tirage désactivées,
  `short_stitch=disabled` en satin) + `project export`. Seul l'export est chronométré.
- Non normalisés : coupes/verrous/finitions, ordre de couture, découpe des satins larges.
- Qualité : `coverage.py` décode le DST (décodeur validé : mêmes nombres de stitches et mêmes
  dimensions que `openstitch-cli stats` pour les deux outils), trace chaque point cousu avec un fil
  de 0,5 mm (hypothèse) et compare au masque de la forme. Orientation Y calibrée sur un remplissage
  (`p_with_hole`), pas sur les satins comparés. La valeur absolue n'a pas de sens physique ;
  seule la comparaison entre outils est lue.
- Relancer : `make_svg_fixtures.py <dossier>` puis `run_geometry_bench.py` (mêmes options que L1).

### Résultats

| forme__mode | outil | p50 ms | points | coupes | sauts | fil m | couverture | débordement |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| big_concave__fill | zigzag | 26 | 5652 | 0 | 7 | 14.8 | 87.8 % | 1.1 % |
| big_concave__fill | openstitch | 32 | 5384 | 2 | 4 | 14.5 | 86.9 % | 0.0 % |
| crescent__fill | zigzag | 22 | 1556 | 0 | 11 | 3.4 | 86.6 % | 4.5 % |
| crescent__fill | openstitch | 32 | 1452 | 3 | 7 | 3.3 | 82.9 % | 3.5 % |
| narrow_spike__fill | zigzag | 23 | 1169 | 0 | 15 | 2.1 | 77.6 % | 16.7 % |
| narrow_spike__fill | openstitch | 33 | 978 | 2 | 12 | 2.0 | 67.9 % | 19.8 % |
| p_with_hole__fill | zigzag | 23 | 3204 | 0 | 12 | 8.0 | 87.6 % | 1.7 % |
| p_with_hole__fill | openstitch | 33 | 3056 | 4 | 9 | 7.9 | 86.9 % | 0.2 % |
| ring__fill | zigzag | 22 | 2920 | 0 | 5 | 7.0 | 88.2 % | 2.2 % |
| ring__fill | openstitch | 93 | 2748 | 2 | 6 | 6.8 | 86.7 % | 0.3 % |
| star__fill | zigzag | 23 | 2485 | 0 | 7 | 6.1 | 88.4 % | 2.0 % |
| star__fill | openstitch | 33 | 2196 | 3 | 5 | 5.8 | 85.5 % | 0.4 % |
| t_shape__fill | zigzag | 17 | 2791 | 0 | 7 | 6.9 | 87.8 % | 1.6 % |
| t_shape__fill | openstitch | 33 | 2646 | 2 | 3 | 6.7 | 86.6 % | 0.1 % |
| u_shape__fill | zigzag | 17 | 3251 | 0 | 7 | 8.0 | 87.9 % | 1.7 % |
| u_shape__fill | openstitch | 32 | 3063 | 2 | 5 | 7.7 | 86.6 % | 0.2 % |
| crescent_thin__satin | zigzag | 33 | 1522 | 0 | 7 | 3.4 | 98.6 % | 3.8 % |
| crescent_thin__satin | openstitch | 33 | 1796 | 1 | 1 | 6.6 | 96.7 % | 3.2 % |
| narrow_spike__satin | zigzag | 45 | 1065 | 0 | 7 | 2.1 | 84.0 % | 18.8 % |
| narrow_spike__satin | openstitch | 32 | 1105 | 1 | 1 | 4.0 | 84.0 % | 16.4 % |
| p_thin__satin | zigzag | 23 | 1128 | 0 | 7 | 6.2 | 99.3 % | 2.2 % |
| p_thin__satin | openstitch | 33 | 1493 | 4 | 8 | 5.9 | 99.0 % | 2.2 % |
| ring_thin__satin | zigzag | 22 | 1174 | 0 | 7 | 7.0 | 99.4 % | 1.8 % |
| ring_thin__satin | openstitch | 33 | 1700 | 4 | 8 | 6.9 | 99.3 % | 2.5 % |
| t_thin__satin | zigzag | 28 | 799 | 0 | 7 | 4.6 | 100.0 % | 0.4 % |
| t_thin__satin | openstitch | 33 | 943 | 1 | 1 | 4.4 | 99.8 % | 0.7 % |
| u_thin__satin | zigzag | 33 | 1064 | 0 | 7 | 5.9 | 99.4 % | 1.0 % |
| u_thin__satin | openstitch | 33 | 1145 | 3 | 8 | 5.6 | 99.7 % | 0.9 % |

### Lecture prudente

- **Temps** : tous les cas sont sous 100 ms et dominés par le démarrage (pas de 5 ms du sondage) ;
  aucun classement de vitesse. Un banc de moteur demande des cas de 100k-250k points.
- **Satin sur formes fines (colonnes de 5-6 mm)** : couverture quasi identique des deux côtés
  (96,7 à 100 %). Aucune preuve ici que la découpe en colonnes de ZigZag couvre mieux les anneaux,
  T ou P qu'OpenStitch ; ce n'est pas un argument pour L2 sur ce corpus.
- **Écart de points non expliqué** : sur `ring_thin`, `p_thin`, `crescent_thin`, OpenStitch émet
  plus de points (1700 contre 1174) à espacement nominal égal. Cause non établie (découpe des
  traversées, fusion des colonnes, réglage résiduel ?) : à examiner objet par objet avant de conclure.
- **Satin large** : ZigZag remplace un satin dont une partie dépasse environ 10 mm par un
  remplissage (`zigzag info` : « Use Fill; it's filled instead ») ; les formes larges ne testent donc
  pas son satin. D'où les variantes fines. Le comportement d'OpenStitch sur ces largeurs n'est pas
  mesuré ici.
- **Remplissage** : ZigZag couvre 1 à 4 points de plus, avec un débordement de 1 à 4 % contre
  0 à 0,5 % pour OpenStitch (qui semble rentrer le contour). Pur effet de réglage de bord possible ;
  non normalisé.
- **Pointe étroite** (`narrow_spike`) : les deux outils débordent fortement (16 à 20 %) ; la métrique
  est peu fiable à cette échelle (fil de 0,5 mm pour une pointe de 3 mm).
- **Coupes** : ZigZag n'en émet pas, il utilise des sauts ; OpenStitch coupe au-delà du seuil de 3 mm.
  Ne pas comparer les compteurs coupes/sauts sans fixer la même politique.

### Limites

Un seul chemin par fichier ; pas de recouvrements ni de calques ; 8 formes ; aucune validation textile ;
fil de 0,5 mm supposé ; routage caché : voir la section L3 ; cache (L4) non traité.


## L3 — routage caché (premier passage)

Statut : **banc posé, résultats non discriminants**. Aucune conclusion « meilleur routage ».

### Ce que font les deux moteurs (lu dans le code)

- ZigZag (`src/route.rs`) : zone « cachée » = terrain cousu ou recouvert plus tard, grille avec
  composantes connexes, A* pondéré (pénalité de bord). `--over-sewn` autorise le trajet par-dessus un
  remplissage déjà cousu au lieu de couper. Par défaut, le remplissage du dessous est découpé sous
  l'objet du dessus (`--keep-overlaps` le garde) : observé sur `bridge_under`.
- OpenStitch (`libs/stitch_generation/src/hidden_travel.cpp`) : grille rastérisant les segments
  `TopStitch` ; un écart après la commande `g` ne s'appuie que sur les cellules recouvertes plus tard
  (indice > g) ; refus motivés (`TooLong`, `NoCoverAtEnd`, `NoPath`) ; option
  `allow_same_color_surface` (surface déjà cousue de même fil). Désactivé par défaut
  (`finishing.hidden_travel.enabled = false`). Les deux extrémités de l'écart doivent reposer sur un
  recouvrement ultérieur.

### Protocole

`scripts/bench/zigzag/run_routing_bench.py` : 5 scénarios à calques, mêmes SVG (mm) pour les deux
outils, remplissages normalisés comme en L1. OpenStitch : `import_svg` par calque (couleur par appel),
puis `set_finishing` avec `hidden_travel` (la variante `hidden_travel_max_jump_mm=20` /
`hidden_travel_max_bridge_mm=100` donne le même résultat, non reproduite dans la table).

Fil errant : longueur des segments encore visibles, à plus de 1 mm de toute forme de leur propre
couleur (hypothèses : fil 0,5 mm, bloc de couleur k = calque k, segment visible si son milieu n'est pas
recouvert). Coupes : compteur natif de chaque outil (`zigzag info`, DST d'OpenStitch) ; le décodeur
d'OpenStitch ne reconnaît pas l'encodage des coupes de ZigZag (0 coupe lue dans son DST), donc les
coupes de la table ne sont **pas strictement comparables** entre outils.

### Résultats

| scénario | config | points | coupes natives | sauts DST | long. sauts mm | fil errant mm |
| --- | --- | --- | --- | --- | --- | --- |
| bridge_under | zigzag | 2351 | 1 | 20 | 117 | 0.0 |
| bridge_under | zigzag_oversewn | 2351 | 1 | 20 | 117 | 0.0 |
| bridge_under | openstitch | 2253 | 3 | 9 | 124 | 0.0 |
| bridge_under | openstitch_hidden | 2253 | 3 | 9 | 124 | 0.0 |
| bridge_row | zigzag | 1170 | 3 | 26 | 101 | 0.0 |
| bridge_row | zigzag_oversewn | 1165 | 3 | 22 | 95 | 0.0 |
| bridge_row | openstitch | 972 | 6 | 15 | 125 | 0.0 |
| bridge_row | openstitch_hidden | 972 | 6 | 15 | 125 | 0.0 |
| disjoint_same | zigzag | 1529 | 5 | 37 | 235 | 0.0 |
| disjoint_same | zigzag_oversewn | 1529 | 5 | 37 | 235 | 0.0 |
| disjoint_same | openstitch | 1462 | 6 | 22 | 253 | 0.0 |
| disjoint_same | openstitch_hidden | 1462 | 6 | 22 | 253 | 0.0 |
| stack_row | zigzag | 3683 | 3 | 30 | 156 | 0.0 |
| stack_row | zigzag_oversewn | 3685 | 3 | 24 | 131 | 0.0 |
| stack_row | openstitch | 3702 | 4 | 10 | 106 | 0.0 |
| stack_row | openstitch_hidden | 3702 | 4 | 10 | 106 | 0.0 |
| frame_inner | zigzag | 4084 | 1 | 15 | 67 | 0.0 |
| frame_inner | zigzag_oversewn | 4081 | 1 | 10 | 52 | 0.0 |
| frame_inner | openstitch | 3913 | 3 | 10 | 122 | 0.0 |
| frame_inner | openstitch_hidden | 3913 | 3 | 10 | 122 | 0.0 |

### Lecture prudente

- **Aucune liaison visible** : le fil errant vaut 0 mm partout. Les 5 scénarios ne départagent pas
  les moteurs sur le critère de visibilité ; il faut des cas plus durs (liaisons longues entre îlots
  sur une surface d'une autre couleur, formes à trous, nombreux objets).
- **OpenStitch `hidden_travel`** : l'activer ne change **rien** (DST identique octet pour octet,
  vérifié ; réglage bien enregistré dans le .osp). Le planificateur ne fait donc aucun pont sur ces
  scénarios. Cause non établie : conditions d'extrémités (`NoCoverAtEnd`), seuils, ou option
  `allow_same_color_surface` que l'opération `set_finishing` n'expose pas. `project diagnose` ne
  signale rien. À instrumenter (refus par écart) avant de conclure à une limite du moteur.
- **ZigZag `--over-sewn`** : aucune différence sur `bridge_under` et `disjoint_same` ; réduit la
  longueur des sauts sur 3 scénarios sur 5 (`stack_row` 156 → 131 mm,
  `frame_inner` 67 → 52 mm, `bridge_row` 101 → 95 mm) sans fil errant mesuré. Gain réel mais modeste ;
  effet textile non évalué.
- **Longueur de sauts** : plus faible chez ZigZag sur `frame_inner` (67 contre 122 mm) et plus forte sur
  `stack_row` (156 contre 106 mm). Les ordres de couture et les politiques de coupe ne sont pas les mêmes ;
  ne pas lire cela comme une qualité de routage.
- **Découpe du dessous** (ZigZag) : visible sur `bridge_under` ; OpenStitch coud les deux calques entiers.
  Cela change le nombre de points et l'épaisseur au croisement ; non évalué physiquement.

### Limites et suite

- Une seule configuration `hidden_travel` d'OpenStitch réellement testée, sans `allow_same_color_surface`.
- Pas d'instrumentation des refus du planificateur OpenStitch ; pas de variation de l'ordre des objets.
- Prochain pas utile : exposer ou forcer `allow_same_color_surface` dans le banc, journaliser les refus
  du planificateur, et ajouter des scénarios où un pont caché est géométriquement garanti (extrémités
  sous un recouvrement ultérieur). Aucune PR de code moteur justifiée par ces mesures.
