# R02 — Calcul GPU, optimisation et performances du moteur

Chantier du 2026-11-17, branche `perf/gpu-compute-r02`. Suite de la campagne
[R01](../../validation/r01/README.md) (profilage CPU et prototypes D3D11).

## En bref

- **Gains mesurés, sortie identique octet pour octet** (A/B entrelacé, p50 chauds, 16 fils) :
  marine −58 % (179 → 74 ms), tentabrode −20 % (270 → 216 ms), texte −20 % (40 → 32 ms), logo
  inchangé. Sur un seul fil : −33 %, −6 %, −9 %.
- **Origine des gains** : deux réécritures algorithmiques exactes, puis du parallélisme
  déterministe. Simulation du tirage du tissu (`PullField`) et grille de recouvrement des ponts
  cachés, qui testait toute la boîte englobante de chaque point.
- **GPU** : le prototype D3D11 de R01, rejoué sur une RTX 5060 Ti de bureau, reste 3 à 5 fois
  plus lent que le CPU, transferts compris. Aucun backend GPU n'est ajouté au moteur ;
  l'affichage de Studio reste sur le scene graph RHI/D3D11.

## Documents

| Document | Contenu |
|---|---|
| [architecture-audit.md](architecture-audit.md) | chaîne image → DST, parallélisme, caches, constats de conception |
| [baseline.md](baseline.md) | machine, protocole, mesures de référence immuables |
| [research.md](research.md) | état de l'art (backends, algorithmes parallèles, références) et addendum mesuré |
| [backend-comparison.md](backend-comparison.md) | CPU, D3D11, Vulkan, QRhi, CUDA : grille et décision |
| [opportunities.md](opportunities.md) | matrice par poste : CPU existant, réécriture, GPU |
| [gpu-architecture.md](gpu-architecture.md) | architecture retenue et contrat d'un futur backend |
| [benchmark-results.md](benchmark-results.md) | A/B entrelacé, détail par étape, mémoire, GPU de bureau |
| [validation.md](validation.md) | preuves d'exactitude et limites |
| [roadmap.md](roadmap.md) | lots suivants, classés |

## Reproduire

```powershell
cmake --preset msvc
cmake --build --preset msvc-release --target openstitch-profile-engine test_stitch openstitch-compute-prototype
$bench = 'build/msvc/tests/bench/Release/openstitch-profile-engine.exe'
powershell -File scripts/performance/profile_engine.ps1 -Executable $bench -OutputDirectory build/r02-run
python scripts/performance/stage_breakdown.py build/r02-run
python scripts/performance/compare_fingerprints.py build/r02-baseline-native build/r02-run
python scripts/performance/ab_interleave.py --a <exe baseline> --b $bench --out build/r02-ab `
  --text-bitmap build/r02-run/text-240mm.bmp
build/msvc/tests/compute_experiment/Release/openstitch-compute-prototype.exe
```

Données brutes : [data/r02-campaigns.zip](data/r02-campaigns.zip). L'archive contient la
baseline (natif et un fil), la campagne finale, l'A/B entrelacé `r02-ab2` et le prototype D3D11.
La campagne finale monofil a été perturbée par une suite de tests d'une autre session
(tentabrode p95 1 263 ms). Elle est conservée telle quelle, mais seul l'A/B fait foi pour les
comparaisons.
