# R02 — Mesures de référence (baseline)

Baseline immuable du chantier R02 : `main` au commit
`beb5738095b72d7f028637c21bcfd6d95821d955`, avant toute modification.

## Machine

- AMD Ryzen 7 7700X, 8 cœurs / 16 fils, 15 Go de RAM, Windows 11 Pro (build 26200).
- NVIDIA GeForce RTX 5060 Ti 16 Go (pilote 610.88) et Radeon intégrée (32.0.21030.2001).
- MSVC (Visual Studio 18 2026), build Release (`msvc-release`), OpenCV 4.12 sans IPP ni
  backend parallèle.
- D'autres sessions de développement pouvaient tourner sur la machine ; aucune compilation
  pendant les mesures. Les p95 sont plus sensibles à cette charge que les p50.

## Protocole

Banc R01 inchangé ([README R01](../../validation/r01/README.md)) :

```powershell
cmake --preset msvc
cmake --build --preset msvc-release --target openstitch-profile-engine
$bench = 'build/msvc/tests/bench/Release/openstitch-profile-engine.exe'
powershell -File scripts/performance/profile_engine.ps1 -Executable $bench -OutputDirectory build/r02-baseline-native
powershell -File scripts/performance/profile_engine.ps1 -Executable $bench -OutputDirectory build/r02-baseline-serial -Threads 1
python scripts/performance/stage_breakdown.py build/r02-baseline-native build/r02-baseline-serial
```

Quatre motifs (logo 100 mm, texte satin 240 mm, tentabrode 300 mm satin, marine 160 mm avec
traits et simulation du tirage sur lin). Trois processus par motif, un passage froid puis dix
chauds par processus : 30 échantillons chauds par distribution. Le parcours mesuré va du
décodage de l'image à l'aperçu rastérisé en mémoire. Il comprend segmentation, vectorisation,
numérisation, séquence effective avec finitions et, pour marine, simulation du tirage. Le cache
de fragments est désactivé pour reconstruire toute la séquence à chaque passage.
`stage_breakdown.py` donne les médianes chaudes par étape. La somme des tâches (`task_ms`)
additionne des appels qui peuvent se recouvrir en parallèle.

Données brutes : [`data/r02-campaigns.zip`](data/r02-campaigns.zip) (`baseline-native`,
`baseline-serial`).

## Bout en bout (ms, chaud)

| Motif | Régions / objets / points | Natif p50 / p95 | Un fil p50 / p95 |
|---|---|---:|---:|
| Logo | 3 / 2 / 2 822 | 10,53 / 12,24 | 8,91 / 11,67 |
| Texte | 25 / 22 / 3 898 | 43,78 / 75,53 | 77,64 / 115,74 |
| Tentabrode | 201 / 140 / 47 074 | 303,30 / 357,15 | 499,44 / 562,55 |
| Marine | 18 / 19 / 22 385 | 178,58 / 190,96 | 188,59 / 200,14 |

## Décomposition (médianes chaudes, natif ; ms)

| Motif | Numérisation | Séquence | dont finitions | Tirage | Aperçu |
|---|---:|---:|---:|---:|---:|
| Logo | 5,2 | 0,8 | — | — | 4,6 |
| Texte | 23,8 | 12,6 | 11,5 | — | 7,3 |
| Tentabrode | 139,9 | 123,5 | 68,0 | — | 34,7 |
| Marine | 20,0 | 34,9 | 29,4 | **95,4** | 28,6 |

Somme des tâches, tentabrode natif : squelette 111,6 ms (106 appels), tatami 77,9, finitions
68,0, liaisons cachées 60,4 (296 appels, construction de la grille comprise, voir
[audit](architecture-audit.md)), segmentation 53,3, numérisation 52,4, distance 49,6, lissage
44,5, décodage 19,1, vectorisation 19,1, médian 10,7.

Mémoire : pic RSS 17 Mio (logo), 41 Mio (texte), 153 Mio (tentabrode), 31 Mio (marine).
VRAM : aucune utilisée par le moteur.

## Empreintes

Identiques sur les 264 passages (froids et chauds, deux réglages de fils) :

| Motif | Séquence |
|---|---|
| Logo | `10305887812668566756` |
| Texte | `12776455089310209455` |
| Tentabrode | `12614204738498221751` |
| Marine | `8956883305371479416` |

Les empreintes d'étiquettes et d'aperçu sont dans les données brutes. L'aperçu de marine est
rendu à partir de la séquence simulée et contrôle donc aussi la simulation du tirage.
