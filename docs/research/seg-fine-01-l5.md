# SEG-FINE-01 — Masques de référence et profilage CPU

Baseline moteur `9ade411a`. Windows Release, OpenCV 4.12, Ryzen 7 7700X. Le banc
`openstitch-seg-fine-bench` appelle le moteur sans Qt ni écriture OSP dans la mesure.
Il chauffe les modes OFF et ON, alterne leur ordre et vérifie la reproductibilité des labels
et du masque à chaque répétition. Le temps du détecteur seul est mesuré dans un second appel.
La durée de morphologie correspond au chronométrage des Top Hats du chemin ON.

## Reproduction

```powershell
cmake --preset msvc
cmake --build --preset msvc-release --target openstitch-seg-fine-bench
$bench = 'build/msvc/tests/bench/Release/openstitch-seg-fine-bench.exe'
& $bench IMAGE_SOURCE MASQUE.png 5 18 10
```

Le banc écrit le masque de protection ON en PNG et les mesures par exécution en CSV sur stdout.
Les deux derniers arguments sont les seuils haut et bas. Pour les photos, télécharger les
originaux indiqués dans [les annotations](../../tests/fixtures/ai-benchmark/seg-fine-01/annotations.json),
appliquer l'orientation EXIF, convertir en RVB et redimensionner avec Lanczos à 960 px sur le côté
long avant d'écrire le PNG. Le script de mesure vérifie les SHA-256 des entrées annotées.
Sur la marine synthétique, réutiliser le PNG source et les masques versionnés du dépôt.

Pour chaque seuil haut 18, 24 et 32, nommer les masques `voilier-SEUIL.png`, `peche-SEUIL.png`
et `marine-SEUIL.png`. Les photos préparées s'appellent `voilier.png` et `peche.png`.

```powershell
python tests/fixtures/ai-benchmark/seg-fine-01/measure_masks.py DOSSIER_PHOTOS DOSSIER_MASQUES
```

## Détection CPU

Sur la marine synthétique, le rappel du masque est de 100 % au trait. La précision est de
19,4 % sur les pixels évalués, et le taux de faux positifs dans ciel/eau vaut 1,14 %.
Seuls les pixels annotés gréement ou ciel/eau hors du voisinage du mât et du gréement sont
évalués. Ce score décrit le masque de protection. Le rendu final peut réaffecter une partie
des pixels à la bonne couleur, comme le montre le [comparatif des bateaux](seg-fine-01-bateaux.md).

Sur les photos, les tracés manuels sont des axes approximatifs, évalués à un pixel de tolérance.
Sur le bateau de pêche, l'antenne et l'amarre sont couvertes sur 100 % de leur axe tracé.
Le hauban pâle tracé du Mirage n'est pas retrouvé. Le masque couvre 76,0 % de la zone d'eau du
Mirage et 78,8 % de celle du bateau de pêche, ainsi que 79,4 % de la zone de feuillage du bateau
de pêche. Les deux zones témoins de ciel sont intactes. Les seuils hauts 24 et 32 donnent les
mêmes résultats que 18 sur ces annotations. La hausse du seuil de germe seule n'est pas retenue.

Avec un seuil bas de 16 et un seuil haut de 18, la couverture parasite de l'eau descend
à 11,0 % sur le Mirage et 6,9 % sur le bateau de pêche. Le feuillage descend à 29,3 %.
L'antenne reste couverte, l'amarre passe de 100 % à 98,2 % et le hauban pâle reste absent.
Le seuil bas n'est donc pas changé dans le produit. Les résultats motivent une variante
directionnelle à éprouver sur un corpus plus large.

## Profilage initial GPU-01

| Cas | Segmentation OFF | Segmentation ON | Détecteur seul | Morphologie ON |
| --- | ---: | ---: | ---: | ---: |
| Mirage 846 × 960 | 49,5 ms | 921,0 ms | 823,5 ms | 799,8 ms |
| Pêche 960 × 720 | 39,3 ms | 786,0 ms | 701,9 ms | 681,2 ms |
| Marine 320 × 240 | 9,1 ms | 92,7 ms | 81,1 ms | 76,4 ms |
| Pêche redimensionnée en 4K | 217,3 ms | 8 680,9 ms | 8 285,7 ms | 8 174,1 ms |
| Marine redimensionnée en 4K | 183,6 ms | 8 551,4 ms | 8 282,4 ms | 8 145,2 ms |

Médianes sur cinq répétitions alternées. La photo 4K est redimensionnée à 3840 × 2160 pour
charger le moteur, pas pour qualifier la qualité des masques. La morphologie est le premier
bloc à comparer à un prototype D3D11 résident. Les temps ci-dessus mesurent exclusivement le
CPU. Toute décision d'intégration demande une parité des masques, puis des mesures GPU
avec les transferts et le repli inclus.

## Optimisation CPU

Les trois plans Lab sont traités en parallèle à chaque échelle. Les opérations OpenCV et l'ordre
d'exploitation des réponses restent les mêmes. Les temps ci-dessous concernent la segmentation
ON complète, sans écriture de fichier.

Sur les deux photos et la marine, les entrées `segmentation.u32` et `project.json` des OSP
sont identiques entre la baseline et les exécutions optimisées à 1 et 16 travailleurs, en OFF
et en ON.

| Cas | CPU initial | CPU avec canaux parallèles |
| --- | ---: | ---: |
| Mirage | 921,0 ms | 399,3 ms |
| Pêche | 786,0 ms | 357,2 ms |
| Marine | 92,7 ms | 43,1 ms |
| Pêche 4K | 8 680,9 ms | 3 447,3 ms |
| Marine 4K | 8 551,4 ms | 3 391,6 ms |

La morphologie prend encore 2,97 à 3,00 s sur les deux entrées 4K. L'optimisation CPU sert
de nouvelle référence pour le prototype GPU. Les [mesures brutes](../../tests/fixtures/ai-benchmark/seg-fine-01/measurements.json)
incluent les répétitions individuelles et les scores des masques.

## Vérification matérielle D3D11

```powershell
cmake --build --preset msvc-release --target openstitch-compute-prototype
build/msvc/tests/compute_experiment/Release/openstitch-compute-prototype.exe --segmentation
```

Le prototype R01 vérifie l'égalité octet pour octet des sorties sur cinq répétitions.
Mesures de bout en bout à chaud sur RTX 5060 Ti, allocations, envoi et lecture compris.

| Ouverture d'un plan synthétique | CPU médian | GPU médian |
| --- | ---: | ---: |
| Ellipse 9 × 9, image 960 × 720 | 22,58 ms | 2,69 ms |
| Ellipse 17 × 17, image 480 × 360 | 20,58 ms | 1,25 ms |

Initialisation du contexte et compilation des shaders en environ 186 ms, mesurées séparément.
Ce contrôle éprouve des ouvertures individuelles sur les tailles admissibles par le banc R01.
Il reste à qualifier les ouvertures et fermetures chaînées sur les trois plans Lab, la fusion
des Top Hats, la résidence des données à toutes les échelles et la parité de la segmentation.
