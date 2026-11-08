# R01-03 — Prototype D3D11 pour masques et distances

Le banc appelle le même contrat hôte en CPU et en GPU. Les comparaisons
sont faites avec l'ouverture elliptique et la distance `DIST_L2, 3`
d'OpenCV. Les sorties doivent concorder octet pour octet pour les
masques et bit à bit pour les flottants en pixels. Les jeux limites
couvrent tous les masques 3 × 3 binaires, les diamètres pairs et impairs,
les trous, les traits fins, les valeurs grises, les bords, les masques
sans zéro et les dimensions proches d'une taille de groupe.
Les ouvertures de petit diamètre sont comparées jusqu'à 2 048 × 2 048.
La distance est comparée sur des bandes de 2 048 pixels et des masques
denses jusqu'à 512 × 511.

Le noyau de distance examine les lignes par écart vertical croissant.
Un premier passage donne, pour chaque pixel de chaque ligne, l'écart
horizontal au zéro le plus proche. Le second calcule le minimum de la
métrique chanfrein Q16. Pour des écarts `dx` et `dy`, le coût vaut
`62587 × max(dx, dy) + 27151 × min(dx, dy)`. Un écart vertical suffit
à arrêter la recherche quand sa borne `62587 × dy` atteint le meilleur
coût déjà trouvé. [Référence OpenCV 4.12 sans IPP](https://github.com/opencv/opencv/blob/4.12.0/modules/imgproc/src/distransform.cpp#L45-L129).

## Reproduire

```powershell
cmake --preset msvc
cmake --build --preset msvc-release --target openstitch-compute-prototype test_compute_experiment
ctest --test-dir build/msvc/tests/compute_experiment -C Release --output-on-failure
$exe = 'build/msvc/tests/compute_experiment/Release/openstitch-compute-prototype.exe'
& $exe --cpu-only
& $exe | Set-Content -Encoding utf8 build/r01-d3d11.jsonl
```

Le banc requiert Windows, le SDK D3D11 et un adaptateur matériel de niveau
11.0. La cible est exclue du build par défaut. Le test hôte indépendant
est disponible dans le build de test sans Qt. Le mode `--cpu-only`
exerce le repli automatique en omettant l'adaptateur.

Le prototype échange des buffers hôte pour chaque appel. Le banc
mesure le temps mur entre ces buffers, avec la préparation, les
transferts et la synchronisation. Il alterne l'ordre CPU/GPU au fil
des répétitions. Les résultats du lot sont conservés jusqu'à la fin du
chronométrage, puis comparés bit à bit à la référence CPU à chaque
répétition. Les masques synthétiques reproduisent les tailles et
les volumes d'appels choisis à partir du
[profilage CPU](https://github.com/ElZacho200a/OpenStitch/blob/17839bf1/docs/validation/r01/README.md).
Ils contiennent des motifs disjoints, des trous et des traits qui
touchent le cadre. La mesure concerne le prototype et la charge
utile de ses appels. Une décision d'intégration demanderait ensuite
la latence et les empreintes du pipeline complet.

`upload_ms` couvre la préparation et l'envoi côté hôte.
`readback_ms` couvre la copie vers le tampon de lecture, l'attente
et la conversion en sortie. Le timestamp GPU `dispatch_ms` peut
recouvrir cette attente. Le temps mur est la mesure de référence.

Le prototype borne les buffers et le nombre de visites calculées avant
dispatch. Le budget de 128 Mio couvre les tampons comptabilisés. La
borne de 134 217 728 visites couvre deux voisinages pour l'ouverture
ou les lignes candidates et le prépassage pour la distance.
Le banc vérifie le refus de deux requêtes coûteuses de 2 048 × 2 048
en mode GPU forcé et le repli CPU bit à bit pour la distance en mode
automatique. Une requête annulée pendant la lecture bloquante D3D11
attend le retour du pilote avant d'écarter son résultat.

## Campagne du 8 novembre 2026

[Échantillons bruts, métadonnées, synthèse et relevé de charge](2026-11-08-d3d11-data.zip).
Windows 11 (26200), MSVC 19.44 Release, OpenCV 4.12 sans IPP, RTX 4060
Laptop, pilote 592.82, code `955d12412c91315eccd5ea08350bdc057605cafd`.
Trois processus et 21 répétitions par scénario et par backend, soit
63 mesures pour chaque distribution. L'initialisation du contexte et
des shaders prend 226 ms en médiane, puis le premier retour GPU 6,62 ms.

| Lot synthétique | CPU p50 / p95 | GPU p50 / p95 | Préparation GPU p50 | Noyaux GPU p50 | Retour et attente p50 | Charge GPU envoyée + lue |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Ouverture marine, 1 masque | 0,34 / 0,49 ms | 1,15 / 1,95 ms | 0,62 ms | 0,34 ms | 0,52 ms | 0,61 Mo |
| Distance texte, 22 masques | 4,92 / 7,68 ms | 28,84 / 43,42 ms | 14,29 ms | 8,40 ms | 11,89 ms | 13,73 Mo |
| Distance Tentabrode, 106 masques | 26,17 / 36,91 ms | 136,41 / 163,96 ms | 60,11 ms | 41,81 ms | 57,43 ms | 65,40 Mo |

Les 16 254 résultats chronométrés, dont 8 127 GPU, concordent bit à bit
avec la référence CPU. Les trois processus ont aussi réussi les 4 665
comparaisons de cas limites et le repli CPU sur le masque de 2 048 × 2 048
refusé par la limite de travail. Les 11 tests du contrat CPU passent en
Debug et Release. Le mode sans adaptateur a été exercé sur Windows
avec l'adaptateur omis par le banc.

Un passage distinct avec NVML a relevé 34 échantillons à 200 ms. La
charge globale de la même RTX variait de 0 à 35 %, les états de
performance de P0 à P5 et l'horloge SM de 855 à 2 250 MHz. Les courtes
passes D3D11 alternent avec la préparation hôte, l'attente et la lecture
des résultats. NVML agrège la charge de tout l'adaptateur sur une
fenêtre propre au produit. Ce relevé montre les variations de charge
et d'horloge. Il ne permet pas d'attribuer les écarts de latence à
une cause unique. [Définition de l'utilisation NVML](https://docs.nvidia.com/deploy/nvidia-smi/index.html#utilization).

## Décision

**No-go pour l'intégration de ces deux noyaux D3D11 avec transfert
synchrone par masque.** L'ouverture est environ quatre fois plus
lente que le CPU au p95 sur le lot marin. Pour le texte, les
43,42 ms GPU au p95 dépassent d'environ 38 fois le budget de 1,15 ms
du profilage CPU. Les 8,40 ms médians des noyaux dépassent déjà ce
budget. Ce verdict concerne l'architecture et le matériel mesurés.
L'intégration des remplissages et le retour interactif Studio
restent conditionnés à un gain de bout en bout et à la parité sur
le parcours complet.
