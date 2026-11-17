# R02 — Résultats après implémentation

Machine et protocole : voir la [baseline](baseline.md). Données brutes :
[`data/r02-campaigns.zip`](data/r02-campaigns.zip).

## Mesure de référence : A/B entrelacé

La machine était partagée avec d'autres sessions de développement (compilations, suites de
tests) : des campagnes séparées dans le temps ne sont pas comparables au p95. La comparaison de
référence fait donc tourner en **alternance**, processus par processus (ordre A,B puis B,A),
l'exécutable de la baseline (A, `beb57380`, compilé dans un worktree détaché) et celui de la
branche (B). Six processus × onze passages par côté, soit 60 échantillons chauds par côté et
par motif. B/A est le rapport des médianes, avec un intervalle de confiance bootstrap à 95 %
(2 000 tirages, graine fixe).

```powershell
python scripts/performance/ab_interleave.py --a <base>/openstitch-profile-engine.exe `
  --b build/msvc/tests/bench/Release/openstitch-profile-engine.exe --out build/r02-ab2-native `
  --processes 6 --text-bitmap build/r02-baseline-native/text-240mm.bmp
# idem avec --threads 1 et --out build/r02-ab2-serial
```

Un premier passage (`r02-ab-*`) a été **écarté** : une seconde exécution du même script, lancée
en même temps par une autre session, avait écrit dans les mêmes dossiers. Le script refuse
désormais un dossier de sortie existant et horodate chaque campagne ; les chiffres ci-dessous
viennent de `r02-ab2-native` (12:02:46 → 12:03:51) et `r02-ab2-serial` (12:03:51 → 12:05:44).

### Réglage de fils natif (16)

| Motif | A p50 / p95 (ms) | B p50 / p95 (ms) | B/A [IC 95 %] |
|---|---:|---:|---:|
| Logo | 10,60 / 13,66 | 10,33 / 14,00 | 0,974 [0,940 ; 1,005] |
| Texte | 40,50 / 53,29 | 32,44 / 37,36 | **0,801** [0,783 ; 0,832] |
| Tentabrode | 270,26 / 305,84 | 215,63 / 237,45 | **0,798** [0,789 ; 0,811] |
| Marine | 178,89 / 184,29 | 74,25 / 76,59 | **0,415** [0,410 ; 0,420] |

### Un seul fil

| Motif | A p50 / p95 (ms) | B p50 / p95 (ms) | B/A [IC 95 %] |
|---|---:|---:|---:|
| Logo | 8,79 / 10,35 | 8,67 / 9,02 | 0,986 [0,974 ; 1,000] |
| Texte | 65,40 / 69,63 | 59,69 / 68,15 | **0,913** [0,905 ; 0,950] |
| Tentabrode | 514,64 / 560,04 | 483,53 / 552,52 | **0,940** [0,917 ; 0,972] |
| Marine | 178,01 / 182,30 | 119,44 / 122,15 | **0,671** [0,667 ; 0,675] |

Le logo ne passe ni par la simulation du tirage ni par les ponts cachés : rapport compatible
avec 1 dans les deux réglages. Les machines à un cœur gagnent aussi, grâce aux seules réécritures algorithmiques.

## Par étape (A/B `r02-ab2`, médianes chaudes, ms)

| Étape | Motif | Natif A → B | Un fil A → B |
|---|---|---:|---:|
| Simulation du tirage | marine | 94,8 → 14,1 | 91,9 → 47,9 |
| Finitions | tentabrode | 64,6 → 13,1 | 64,2 → 40,0 |
| Finitions | marine | 29,8 → 5,7 | 27,9 → 13,7 |
| Finitions | texte | 10,9 → 2,6 | 10,3 → 5,5 |
| Séquence effective complète | tentabrode | 116,0 → 62,8 | 138,1 → 111,4 |

Progression mesurée sur tentabrode : la grille par intervalle utile seul ramène sa construction
de 52,5 à 39,5 ms (monofil, sonde temporaire). Le carré de la distance la ramène ensuite à
environ 25 ms, et les bandes parallèles portent les finitions à 13,6 ms. Un premier découpage
à 64 bandes fixes ralentissait le monofil (finitions 62,6 ms) ; il est remplacé par une bande
unique à un fil et des étendues de rangées calculées une fois.

## Mémoire

Pic RSS (maximum sur l'A/B `r02-ab2`) : +2 à +3 Mio en natif (tentabrode 161,6 → 164,6 Mio,
marine 32,0 → 34,1, texte 43,2 → 44,8), inchangé à un fil à la précision du banc (tentabrode
109,7 → 108,8). Les structures ajoutées sont linéaires dans le nombre de segments : index CSR et
grandeurs précalculées de `PullField`, étendues de rangées de la grille, plans pré-calculés des
ponts. Aucune VRAM.

## GPU D3D11 sur GPU de bureau

Banc `openstitch-compute-prototype` de R01, rejoué sans modification sur la RTX 5060 Ti
(pilote 610.88) : 3 processus × 21 répétitions par lot et par backend ; 63 mesures par
distribution, toutes exactes bit à bit.

| Lot | CPU p50 / p95 | D3D11 p50 / p95 | Envoi + retour p50 |
|---|---:|---:|---:|
| Ouverture marine, 1 masque | 0,21 / 0,22 ms | 0,62 / 0,91 ms | 0,60 ms |
| Distance texte, 22 masques | 4,08 / 5,73 ms | 19,97 / 25,11 ms | 16,4 ms |
| Distance tentabrode, 106 masques | 18,66 / 22,14 ms | 89,67 / 103,71 ms | 73,2 ms |

Initialisation du contexte : 176 à 221 ms. Le GPU de bureau divise environ par 1,5 les temps du
GPU portable de R01, mais les transferts seuls coûtent encore 4 fois le calcul CPU complet :
**no-go confirmé** pour ces noyaux avec transferts par masque.
