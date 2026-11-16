# Banc des recettes de points (STCH-CROS-01)

Généré par `python scripts/recipe_bench.py` (export = génération complète depuis l'OSP puis écriture DST, temps mur, binaire Debug (MSVC), temps mur incluant le lancement du processus).

| recette | forme | état | points | sauts | coupes | export (ms) |
|---|---|---|---|---|---|---|
| running-stitch | rect30x20 | ok | 48 | 1 | 1 | 54 |
| running-stitch | ellipse40x30 | ok | 53 | 1 | 1 | 53 |
| running-stitch | concave_L | ok | 72 | 1 | 1 | 53 |
| running-stitch | star | ok | 88 | 1 | 1 | 54 |
| running-stitch | rect240 | ok | 392 | 1 | 1 | 50 |
| backstitch | rect30x20 | ok | 109 | 1 | 1 | 50 |
| backstitch | ellipse40x30 | ok | 121 | 1 | 1 | 95 |
| backstitch | concave_L | ok | 169 | 1 | 1 | 51 |
| backstitch | star | ok | 197 | 1 | 1 | 51 |
| backstitch | rect240 | ok | 969 | 1 | 1 | 51 |
| stem-stitch | rect30x20 | ok | 48 | 1 | 1 | 52 |
| stem-stitch | ellipse40x30 | ok | 53 | 1 | 1 | 164 |
| stem-stitch | concave_L | ok | 72 | 1 | 1 | 51 |
| stem-stitch | star | ok | 88 | 1 | 1 | 53 |
| stem-stitch | rect240 | ok | 392 | 1 | 1 | 51 |
| holbein-stitch | rect30x20 | ok | 88 | 1 | 1 | 52 |
| holbein-stitch | ellipse40x30 | ok | 98 | 1 | 1 | 54 |
| holbein-stitch | concave_L | ok | 136 | 1 | 1 | 53 |
| holbein-stitch | star | ok | 168 | 1 | 1 | 52 |
| holbein-stitch | rect240 | ok | 776 | 1 | 1 | 156 |
| split-stitch | rect30x20 | ok | 145 | 1 | 1 | 54 |
| split-stitch | ellipse40x30 | ok | 157 | 1 | 1 | 157 |
| split-stitch | concave_L | ok | 225 | 1 | 1 | 52 |
| split-stitch | star | ok | 257 | 1 | 1 | 51 |
| split-stitch | rect240 | ok | 1289 | 1 | 1 | 168 |
| blanket-stitch | rect30x20 | ok | 128 | 1 | 1 | 55 |
| blanket-stitch | ellipse40x30 | ok | 140 | 1 | 1 | 50 |
| blanket-stitch | concave_L | ok | 200 | 1 | 1 | 53 |
| blanket-stitch | star | ok | 224 | 1 | 1 | 53 |
| blanket-stitch | rect240 | ok | 1160 | 1 | 1 | 54 |
| zigzag-row | rect30x20 | ok | 140 | 1 | 1 | 52 |
| zigzag-row | ellipse40x30 | ok | 156 | 1 | 1 | 51 |
| zigzag-row | concave_L | ok | 220 | 1 | 1 | 52 |
| zigzag-row | star | ok | 248 | 1 | 1 | 53 |
| zigzag-row | rect240 | ok | 1288 | 1 | 1 | 54 |
| long-and-short-stitch | rect30x20 | ok | 393 | 1 | 1 | 269 |
| long-and-short-stitch | ellipse40x30 | ok | 686 | 1 | 1 | 231 |
| long-and-short-stitch | concave_L | ok | 630 | 1 | 1 | 187 |
| long-and-short-stitch | star | ok | 606 | 1 | 1 | 214 |
| long-and-short-stitch | rect240 | ok | 38461 | 9 | 2 | 11530 |
| laid-work | rect30x20 | ok | 229 | 1 | 1 | 56 |
| laid-work | ellipse40x30 | ok | 428 | 1 | 1 | 55 |
| laid-work | concave_L | ok | 309 | 1 | 1 | 51 |
| laid-work | star | ok | 404 | 4 | 3 | 53 |
| laid-work | rect240 | ok | 18628 | 1 | 1 | 73 |
| darning-stitch | rect30x20 | ok | 131 | 1 | 1 | 54 |
| darning-stitch | ellipse40x30 | ok | 203 | 1 | 1 | 53 |
| darning-stitch | concave_L | ok | 156 | 1 | 1 | 152 |
| darning-stitch | star | ok | 206 | 5 | 3 | 55 |
| darning-stitch | rect240 | ok | 9847 | 1 | 1 | 64 |
| cross-stitch | rect30x20 | ok | 384 | 1 | 1 | 53 |
| cross-stitch | ellipse40x30 | ok | 596 | 1 | 1 | 54 |
| cross-stitch | concave_L | ok | 440 | 1 | 1 | 53 |
| cross-stitch | star | ok | 535 | 16 | 7 | 53 |
| cross-stitch | rect240 | ok | 36776 | 1 | 1 | 88 |
| half-cross-stitch | rect30x20 | ok | 199 | 1 | 1 | 51 |
| half-cross-stitch | ellipse40x30 | ok | 311 | 2 | 2 | 157 |
| half-cross-stitch | concave_L | ok | 231 | 1 | 1 | 53 |
| half-cross-stitch | star | ok | 276 | 9 | 6 | 54 |
| half-cross-stitch | rect240 | ok | 18439 | 1 | 1 | 69 |
| tent-stitch | rect30x20 | ok | 567 | 1 | 1 | 52 |
| tent-stitch | ellipse40x30 | ok | 855 | 3 | 3 | 55 |
| tent-stitch | concave_L | ok | 570 | 1 | 1 | 52 |
| tent-stitch | star | ok | 701 | 11 | 7 | 68 |
| tent-stitch | rect240 | ok | 51207 | 1 | 1 | 90 |
| upright-cross | rect30x20 | ok | 383 | 1 | 1 | 152 |
| upright-cross | ellipse40x30 | ok | 616 | 2 | 2 | 55 |
| upright-cross | concave_L | ok | 486 | 1 | 1 | 55 |
| upright-cross | star | ok | 535 | 7 | 5 | 161 |
| upright-cross | rect240 | ok | 36998 | 1 | 1 | 192 |
| diamond-lattice | rect30x20 | ok | 208 | 1 | 1 | 53 |
| diamond-lattice | ellipse40x30 | ok | 300 | 1 | 1 | 56 |
| diamond-lattice | concave_L | ok | 267 | 5 | 2 | 98 |
| diamond-lattice | star | ok | 303 | 2 | 1 | 55 |
| diamond-lattice | rect240 | ok | 13351 | 21 | 2 | 64 |
| brick-stitch | rect30x20 | ok | 382 | 1 | 1 | 51 |
| brick-stitch | ellipse40x30 | ok | 644 | 1 | 1 | 54 |
| brick-stitch | concave_L | ok | 438 | 1 | 1 | 52 |
| brick-stitch | star | ok | 573 | 4 | 3 | 53 |
| brick-stitch | rect240 | ok | 32194 | 1 | 1 | 83 |
| seeding-stitch | rect30x20 | ok | 471 | 61 | 36 | 54 |
| seeding-stitch | ellipse40x30 | ok | 725 | 95 | 55 | 154 |
| seeding-stitch | concave_L | ok | 506 | 72 | 37 | 76 |
| seeding-stitch | star | ok | 569 | 79 | 43 | 55 |
| seeding-stitch | rect240 | ok | 47234 | 6330 | 3538 | 145 |
| bayeux-stitch | rect30x20 | ok | 479 | 4 | 2 | 53 |
| bayeux-stitch | ellipse40x30 | ok | 769 | 3 | 2 | 55 |
| bayeux-stitch | concave_L | ok | 621 | 5 | 2 | 54 |
| bayeux-stitch | star | ok | 700 | 2 | 1 | 55 |
| bayeux-stitch | rect240 | ok | 38251 | 21 | 2 | 91 |
| trellis-filling-stitch | rect30x20 | ok | 291 | 14 | 10 | 58 |
| trellis-filling-stitch | ellipse40x30 | ok | 415 | 15 | 11 | 53 |
| trellis-filling-stitch | concave_L | ok | 385 | 25 | 18 | 53 |
| trellis-filling-stitch | star | ok | 384 | 33 | 21 | 52 |
| trellis-filling-stitch | rect240 | ok | 20176 | 125 | 87 | 80 |
| trellis-couching | rect30x20 | ok | 196 | 8 | 6 | 55 |
| trellis-couching | ellipse40x30 | ok | 294 | 10 | 8 | 167 |
| trellis-couching | concave_L | ok | 256 | 13 | 10 | 54 |
| trellis-couching | star | ok | 279 | 19 | 14 | 52 |
| trellis-couching | rect240 | ok | 13566 | 69 | 50 | 168 |

Lecture : chaque recette génère sur toutes les formes, y compris le carré de 240 mm (aucun
refus, aucun crash). Le semis (`seeding-stitch`) isole chaque graine et multiplie sauts et
coupes : acceptable sur une petite zone, pas sur un grand aplat tant que les graines ne sont pas
routées. Les vignettes PNG (rendu OpenStitch, ellipse 40 x 30 mm) sont écrites dans `--out` et
ne sont pas versionnées.
