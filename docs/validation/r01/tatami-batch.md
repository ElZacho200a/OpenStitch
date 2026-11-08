# R01-04 — Intersections de tatami par lots

L'expérience compare trois chemins pour produire les abscisses des
intersections. Le premier utilise le balayage actuel. Le deuxième
cherche par dichotomie les rangées couvertes par chaque arête et
interpole seulement ces croisements sur CPU. Le troisième utilise
la même préparation puis envoie le lot d'un remplissage à D3D11.
Chaque arête dispose de slots disjoints dans le résultat compact.
Le lot demande un dispatch et une lecture de retour.

Les rangées gardent l'addition répétée du moteur et les croisements
gardent la convention demi-ouverte. Le shader exécute la soustraction,
la division FP64, la multiplication et l'addition dans cet ordre.
La division exige la capacité D3D11 de doubles étendus. Le tri,
les pénétrations, les sous-couches, le routage, l'arrondi des coordonnées
et les finitions passent par le générateur de production.
[Instruction DDIV](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/ddiv--sm5---asm-).

Le temps mur va du projet déjà numérisé à la séquence effective complète,
avec cache de fragments désactivé. Il comprend les transferts et le coût
de préparation du lot. Les validations ont lieu hors chronométrage.
Le banc vérifie chaque commande, y compris ordre, coordonnées, type,
passe et objet source, à chaque répétition. Il compare aussi le DST et
relève les écarts binaires et absolus des abscisses FP64 au début de
chaque processus. Il simule un échec d'allocation pour vérifier le repli
sur le balayage CPU. Une divergence des commandes ou du DST exclut la
configuration des comparaisons de vitesse.

`sum_adapter_ms` cumule les appels de l'adaptateur, attente éventuelle
du verrou comprise. Plusieurs appels peuvent se recouvrir. Le temps
mur de la séquence reste la mesure de référence. Les bornes de mémoire
s'appliquent à chaque lot.

## Reproduire

```powershell
cmake --preset msvc -DOPENSTITCH_EXPERIMENTAL_TATAMI_SCAN=ON
cmake --build --preset msvc-release --target openstitch-tatami-batch test_stitch
$exe = 'build/msvc/tests/compute_experiment/Release/openstitch-tatami-batch.exe'
& $exe tests/fixtures/cli/logo_small.png 30.48 11
& $exe tests/fixtures/tentabrode.png 119.2106667 11
& $exe --stress 11
```

Le cas synthétique dessine un motif d'environ 300 mm, avec un contour
lobé de 2 048 sommets, neuf trous de 128 sommets et un angle de
0,31 radian. Les mesures à un fil s'obtiennent en définissant
`OPENSTITCH_THREADS=1` avant de lancer le banc. Le mode habituel
reprend le réglage de fils par défaut. La cible de banc et son hook
sont activés par l'option expérimentale.

Le seuil d'intérêt est un gain d'au moins 5 % au p50 et au p95 sur
la génération complète d'un cas éligible, face au meilleur CPU exact.

## Campagne du 8 novembre 2026

[Échantillons, paramètres et synthèse](2026-11-08-tatami-data.zip).
Build MSVC 19.44 Release sous Windows 11 (26200), Ryzen 7 7840HS,
RTX 4060 Laptop, pilote 592.82. Code mesuré
`d98a870db823c545fb9b8c2746a6ca2dc3b2c2e5`.
Chaque distribution contient 33 mesures chaudes (trois processus
de onze répétitions par chemin). Les trois chemins alternent leur
position à chaque répétition. Les temps sont en millisecondes.

| Fils du moteur | Motif | Points | Lots GPU | CPU balayage p50 / p95 | CPU indexé p50 / p95 | D3D11 p50 / p95 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Défaut | Logo | 2 933 | 4 | 0,90 / 1,39 | 0,85 / 1,29 | 1,86 / 2,43 |
| Défaut | Tentabrode | 43 534 | 145 | 47,93 / 54,26 | 46,54 / 49,90 | 52,33 / 71,92 |
| Défaut | Synthétique | 53 611 | 1 | 18,47 / 20,98 | 13,15 / 14,85 | 14,62 / 15,88 |
| 1 | Logo | 2 933 | 4 | 1,10 / 1,51 | 0,99 / 1,82 | 2,44 / 3,57 |
| 1 | Tentabrode | 43 534 | 145 | 99,06 / 109,62 | 96,27 / 121,81 | 137,65 / 168,00 |
| 1 | Synthétique | 53 611 | 1 | 18,07 / 22,80 | 13,22 / 14,76 | 14,72 / 17,99 |

Les 594 séquences chronométrées concordent commande par commande avec le
balayage CPU. Les DST du contrôle initial concordent octet par octet et
les empreintes restent identiques entre réglages de fils. Le test de
repli reproduit les mêmes séquences avec 906 rejets simulés au total.
Sur 112 332 abscisses comparées, 2 292 représentations FP64 diffèrent.
L'écart absolu maximal vaut 2,91 × 10⁻¹¹ µm. Le contrôle des commandes
finales reste nécessaire pour d'autres géométries ou pilotes.

Build des deux bancs D3D11, format 18 et `test_stitch` Release
vérifiés (495 cas, 237 140 assertions). Revue distincte de la convention
demi-ouverte, du repli et du protocole de mesure.

## Décision

**No-go pour le D3D11 par remplissage.** Sur le grand motif synthétique,
un seul lot GPU améliore le p95 du balayage de 24 %, mais le CPU
indexé reste plus rapide. Sur Tentabrode, le GPU augmente le p95
de 33 % par rapport au balayage CPU habituel. Les 145 envois et
lectures restent inclus dans cette latence.

Le CPU indexé réduit le p95 synthétique d'environ 29 % au réglage
habituel. Son effet varie sur le motif réel. Tentabrode gagne 8 % au p95
au réglage habituel et perd 11 % à un fil. La préparation des arêtes
reste donc une piste à évaluer sur d'autres motifs.
