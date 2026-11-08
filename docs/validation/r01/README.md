# R01-01 — Profilage CPU avant expérimentation GPU

Le [chantier R01](https://app.notion.com/p/412bb207e2da813c9851f2d55005f76a)
étudie le budget de latence disponible pour un calcul GPU du moteur.
Ce banc mesure le parcours sans Qt de l'image à la prévisualisation,
sur les appels du CLI et le moteur de rendu logiciel.

## Reproduire

```powershell
cmake --preset msvc
cmake --build --preset msvc-release --target openstitch-profile-engine
$bench = 'build/msvc/tests/bench/Release/openstitch-profile-engine.exe'
pwsh -File scripts/performance/profile_engine.ps1 -Executable $bench -OutputDirectory build/r01-serial -Threads 1
pwsh -File scripts/performance/profile_engine.ps1 -Executable $bench -OutputDirectory build/r01-native
```

Lancer une campagne à la fois sur une machine au repos. Le lanceur rejoue
chaque cas dans plusieurs processus. Le premier passage d'un processus
mesure des caches moteur froids. Le cache disque de Windows peut déjà
contenir les images. Les autres passages bénéficient des caches moteur
internes, avec reconstruction complète de la séquence effective à chaque
passage. Le rapport sépare ces populations et conserve chaque échantillon.

Le parcours applique un filtre médian, segmente, vectorise dans la
numérisation, génère la séquence effective avec finitions et ponts cachés,
puis rasterise l'aperçu à 3 px/mm (limite de 2 millions de pixels). Le cas
lin passe en plus par la simulation du tirage. La durée de bout en bout
couvre ces opérations depuis le décodage jusqu'au raster en mémoire.
Les données identifient les dimensions, le nombre de régions, d'objets, de
points, de sauts et de coupes. Les empreintes contrôlent la reproductibilité
de la segmentation, de la séquence et du raster.

## Lire les mesures

- `wall_ms` est la latence perçue par l'appelant. `cpu_ms` additionne le temps
  de tous les fils du processus. Une résolution limitée du compteur CPU
  peut arrondir des mesures courtes à zéro.
- `task_ms` cumule le temps des appels d'une étape. Les sous-étapes sont
  incluses dans leur parent, et plusieurs appels peuvent se recouvrir en
  parallèle. Les mesures à un fil servent à estimer la borne de gain
  théorique des opérations du moteur. Le rasteriseur de prévisualisation
  utilise son propre nombre de fils. Les mesures avec le nombre de fils
  par défaut servent de référence pour la latence utilisateur.
- `upload_bytes` et `download_bytes` indiquent la charge utile logique des
  opérations instrumentées. Les formats GPU, l'alignement, le regroupement
  des appels et les tampons de transit changeront le coût réel. `buffers`
  compte les sorties allouées explicitement aux sites OpenCV instrumentés.
  Cette mesure couvre une partie des allocations du moteur. Le pic RSS
  couvre tout le processus depuis son démarrage.
- `--no-instrument` désactive l'enregistrement des sous-étapes du moteur.
  Les durées du parcours et les empreintes restent disponibles pour le
  contrôle de l'effet de la mesure.

## Campagne du 8 novembre 2026

[Données brutes, paramètres, statistiques et empreintes](2026-11-08-cpu-data.zip).
Build MSVC 19.44 Release, OpenCV 4.12.0, Windows 11 (26200), Ryzen 7
7840HS, 16 processeurs logiques. La machine comporte une Radeon 780M
(pilote 32.0.11020.1002) et une RTX 4060 Laptop (pilote 592.82). Le SHA
du code mesuré est `1c71a31376dbaf13f9c62fb78a1629cba1f8ddb2`.

Trois processus par motif et réglage, avec 1 passage froid et 10 passages
chauds par processus. Le p95 chaud est le 29ᵉ des 30 échantillons triés.
Les temps et l'écart absolu médian (MAD) sont en millisecondes. Le pic
RSS est le maximum observé sur la campagne en Mio.

| Cas | Régions / objets / points | Froid p50 | Chaud p50 / p95 | MAD chaud | CPU p50 chaud | Pic RSS | Chaud p50 / p95, moteur à 1 fil |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Logo 100 mm | 3 / 2 / 2 933 | 138,58 | 15,03 / 16,34 | 0,39 | 15,63 | 17,41 | 14,22 / 18,15 |
| Texte 240 mm, satin | 25 / 22 / 3 898 | 267,37 | 60,04 / 67,15 | 2,43 | 234,38 | 42,78 | 96,73 / 104,52 |
| Tentabrode 300 mm, satin | 201 / 140 / 47 960 | 1 661,93 | 482,70 / 533,56 | 13,23 | 1 101,56 | 157,70 | 734,67 / 826,86 |
| Marine 160 mm, traits et lin | 18 / 19 / 23 127 | 402,38 | 282,90 / 297,67 | 4,67 | 343,75 | 30,50 | 285,60 / 306,41 |

Les empreintes de segmentation, de séquence effective et d'aperçu sont
identiques sur les 276 exécutions, par motif et entre réglages. Le contrôle
sans enregistrement comporte un processus et trois passages par motif.
Sa taille suffit à comparer les empreintes. Elle est trop faible pour
isoler le coût de l'instrumentation des variations de latence.

### Classement des opérations à étudier

Somme des médianes chaudes avec moteur à un fil, à poids égal pour les
quatre cas. Les étapes englobantes (segmentation complète et
numérisation complète) sont traitées séparément dans les données.

1. **Simulation du tirage** — 151 ms, concentrés sur le scénario lin.
2. **Amincissement du squelette** — 124 ms, dont 99 ms sur Tentabrode.
3. **Rasterisation de l'aperçu CPU** — 123 ms, avec les fils propres au
   rasteriseur. Cette piste relève de l'aperçu et de sa coordination avec Studio.

Le prétraitement médian suit à 100 ms. Les ponts cachés totalisent 154 ms.
Ils restent un coût important du parcours et se prêtent moins directement
à un calcul indépendant par pixel. Le classement donne le temps total
observé dans les opérations, qui borne la part potentiellement économisable.

### Budget pour les deux noyaux demandés

Seuil proposé pour l'expérimentation suivante : baisse de 5 % au p50
et au p95 de bout en bout sur un cas éligible, empreintes identiques et
repli CPU sur les autres cas. À partir de chaque échantillon chaud avec
moteur à un fil, retrancher les temps de distance et d'ouverture
morphologique. Le gain idéal compare les percentiles de cette série
résiduelle à ceux de la série réelle. Le budget vaut le percentile réel
réduit de 5 %, moins le percentile résiduel. Il couvre le calcul GPU, la
préparation, les transferts et la synchronisation.

| Cas | Charge logique distance (appels, Mo) | Ouverture (ms) | Gain idéal p50 / p95 | Budget p50 / p95 |
| --- | ---: | ---: | ---: | ---: |
| Logo | 0 / 0 | 0 | 0 / 0 % | −0,71 / −0,91 ms |
| Texte | 22 / 8,55 | 0 | 6,57 / 6,10 % | 1,52 / 1,15 ms |
| Tentabrode | 106 / 40,83 | 0 | 4,24 / 4,13 % | −5,56 / −7,16 ms |
| Marine | 1 / 0,065 | 0,37 | 0,15 / 0,15 % | −13,86 / −14,86 ms |

Le texte donne un budget serré pour le champ de distance. Avec 8,55 Mo
de charge utile, le budget p95 de 1,15 ms implique déjà 7,4 Go/s
de débit cumulé idéal avant calcul. Le lissage des classes mobilise
29 ms et 101 Mo sur Tentabrode si chaque classe est transférée séparément.
Ces résultats justifient un prototype isolé et des mesures de transfert
sur GPU réel avant toute intégration au moteur. Le modèle à un fil ne
donne pas le gain du parcours multithread, où plusieurs appels se recouvrent.
