# Banc de mesure de la numérisation automatique

Lot AD-1 du chantier AUTO-DIGITIZE (fiche Notion E14). Script : `scripts/autodigitize_bench.py`,
tests : `tests/scripts/test_autodigitize_bench.py`, baseline :
`tests/fixtures/autodigitize-bench/baseline.json`.

## But

Toute modification de l'auto-digitize (choix satin, segmentation, traits, presets…) doit
montrer son effet sur un corpus fixe, contre la version de `main`, avant d'être proposée.
Le banc ne juge pas l'esthétique : il recopie les mesures du moteur et signale les
régressions. Un essai brodé reste nécessaire pour parler de brodabilité.

## Corpus

Neuf images synthétiques de 400 × 400 px à 203,2 dpi, soit **50 mm** de côté, générées à
l'identique (octet pour octet) par le script, sans fichier binaire dans le dépôt :

| cas | ce qu'il éprouve |
|---|---|
| `logo-two-color` | deux aplats qui se chevauchent |
| `ring` | anneau large : satin fermé ou tatami |
| `s-ribbon` | ruban en S de 5 mm : colonne satin courbe |
| `star-outlined` | étoile à contour noir de 0,9 mm |
| `letter-y` | branche en Y de 4 mm : bifurcation |
| `thin-lines` | traits de 0,7 mm sur aplat |
| `small-details` | points de 1,6 et 3 mm |
| `holes` | aplat percé de trois trous |
| `flat-scene` | illustration cinq couleurs |

S'y ajoutent trois images du dépôt, ramenées à une taille cible (40 mm pour le logo, 80 mm pour les deux autres, résolution déduite des pixels) : `tests/fixtures/cli/logo_small.png`,
`tests/fixtures/tentabrode.png`, `tests/fixtures/ai-benchmark/marine-synth-01/source.png`.
L'empreinte SHA-256 de chaque image est consignée : une image modifiée n'est jamais comparée
à une ancienne mesure.

## Chaîne mesurée

Pour chaque cas : `openstitch-cli project digitize` (réglages par défaut, `--dpi` pour les
synthétiques), puis `project check`, `project fidelity` et `project density` en `--json`.

| mesure | source | sens |
|---|---|---|
| objets créés, par type | rapport de numérisation | information |
| points, sauts, coupes, changements de fil, déplacements, points courts | `check` | plus bas |
| erreurs et avertissements qualité | `check` | plus bas |
| part du motif sans fil, trous intérieurs, détails perdus | `fidelity` | plus bas |
| épaisseurs p99 et max, surface en surépaisseur | `density` | plus bas |
| temps de numérisation | horloge | consigné, jamais comparé |

Tolérances (relative, absolue) dans `METRICS` du script. Au-delà : **régression** (code de
sortie 1). En deçà : « moins bien » signalé sans échec. Une mesure absente reste absente
(jamais remplacée par zéro).

## Usage

```powershell
python scripts/autodigitize_bench.py run --cli build/msvc/apps/cli/Release/openstitch-cli.exe --out build/autodigitize-bench --baseline tests/fixtures/autodigitize-bench/baseline.json
```

Sorties : `report.json` (schéma `openstitch.autodigitize-bench/1`), `compare.json`,
`report.md`, et les OSP/DST dans `work/` pour inspection visuelle (`project render`).

Changer volontairement le résultat (amélioration démontrée) : relancer `run` sans
`--baseline`, relire le Markdown, puis remplacer la baseline **dans la même PR** que le
changement de moteur, avec l'explication des écarts.

## Profils

| profil | options ajoutées |
|---|---|
| `default` | aucune (réglages par défaut du moteur : aucun satin) |
| `logo` | `--satin-bands --fill-overlap 0.15 --curve-tolerance 0.1` (préréglage Studio « Logo / aplats ») |
| `logo-compare` | `logo` + `--satin-compare` (AUTO-SATIN-01, choix comparatif de la stratégie de satin) |

## Premiers constats sur la baseline (main 5be7463a, 2026-11-27)

Faits mesurés, à expliquer par les lots suivants, pas encore des défauts établis :

- Profil `logo` : surépaisseur maximale de 8,8 couches sur `flat-scene` (3,9 sans satin),
  5,8 sur `s-ribbon`, 6,7 sur `thin-lines`.
- Le satin perd du détail : 3,6 % du motif de `thin-lines`, 2,2 % de `star-outlined`
  (0,03 % et 0,09 % sans satin).
- Le satin réduit les manques : `s-ribbon` 2,0 % → 0,03 %, `thin-lines` 3,2 % → 0,01 %.
- `small-details` et `holes` ne prennent jamais de satin.
- Aucune erreur qualité sur le corpus. (Une première version mesurait `tentabrode` à 96 dpi,
  soit 372 mm : ses 50 « erreurs » étaient des points hors du cadre de 100 mm, pas un défaut du
  moteur. Les images du dépôt ont désormais une taille cible.)

## Limites

- Réglages par défaut seulement ; les presets Studio et les options expérimentales ne sont
  pas encore balayés.
- Les synthétiques ont des bords nets : les photos et le flou sont couverts par les images du
  dépôt seulement.
- Aucune mesure esthétique ni direction de points : à juger sur les rendus.
- La baseline dépend du moteur de `main` à sa date de génération ; elle se régénère, elle ne
  se corrige pas à la main.
