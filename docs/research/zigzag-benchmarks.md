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
- Manque de la voie « même géométrie » (SVG → points) : OpenStitch n'a pas d'import SVG vectoriel
  identifié ; à trancher avant L2/L3.
- Cas 100k/250k points, 1000 régions réelles, recouvrements, re-numérisation à chaud : non faits.
