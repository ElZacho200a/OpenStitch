---
name: openstitch-cli-debug
description: Reproduire et inspecter visuellement un comportement du moteur de broderie sans l'IHM, avec openstitch-cli (stitchdebug, auto-satin-debug, sgsd-debug, digitize, dst2svg, stats), et régénérer les SVG dorés de tests/golden/. À utiliser pour diagnostiquer un rendu de points bizarre (satin, tatami, sous-couches, routage, jonctions), comparer avant/après un changement de génération, tester l'auto-numérisation sur une image, ou quand l'utilisateur parle de « golden », « SVG de debug » ou d'un DST produit.
---

# Diagnostic visuel via openstitch-cli

La CLI exerce **les mêmes libs que l'app**, sans Qt : c'est le moyen le plus
rapide de reproduire un problème de génération de façon déterministe et
partageable. Les SVG produits sont des références de diagnostic, pas des
tests (aucune comparaison de pixels).

Binaire (après `cmake --build --preset msvc-debug --target openstitch-cli`) :

```powershell
$cli = "build\msvc\apps\cli\Debug\openstitch-cli.exe"
& $cli --help
& $cli <sous-commande> --help
```

Écrire les sorties exploratoires dans le **scratchpad** de la session, jamais
à la racine du dépôt (des `out.svg` / `trident-debug.svg` y traînent déjà —
ne pas en ajouter).

## Sous-commandes

| Besoin | Commande |
|---|---|
| Points courants / tatami sur forme de référence | `stitchdebug --shape line\|corner\|circle\|bezier\|star\|ring --length 3 --repeats 1\|2\|3 [--underlay 1\|2\|3] [--underpath] --output-svg f.svg` |
| Satin auto : squelette, satinabilité, rails/barreaux | `auto-satin-debug --shape rectangle\|capsule\|ribbon\|s\|y\|t\|cross\|h\|circle\|ring\|wide\|tiny\|notch\|pinch\|trident [--pixel-size 0.05] [--cap-end 0\|1\|2] [--short 0\|2\|3] [--split 0..3] [--underlay masque 1/2/4] [--lock 0..3] [--route] [--satin-geometry legacy\|parametric] --output-svg f.svg [--coverage-svg c.svg]` |
| Décomposition SGSD (satin_planning, phases 1–9) | `sgsd-debug --shape trident [--beam-width 3]` (rapport texte : couverture SGSD vs direct) |
| Pipeline image → DST complet | `digitize image.png sortie.dst [--dpi 96] [--max-colors 8] [--min-region-px 16] [--smoothing-px 3] [--skip-background -1\|0\|1] [--output-svg f.svg]` |
| Lire un DST | `stats f.dst` (points, sauts, couleurs, dimensions) ; `dst2svg f.dst f.svg` |
| Métadonnées image | `info image.png [--dpi 96]` |

Image de test réelle : `sample/tentabrode.png` (pour `digitize`).

## Méthode de diagnostic

1. **Reproduire** avec la forme de référence la plus simple qui montre le
   défaut (`trident`/`y`/`cross` pour les jonctions satin, `ring` pour les
   trous, `corner` pour les angles, `pinch`/`notch` pour les rétrécissements).
2. **Comparer avant/après** : générer le SVG sur `HEAD` (ou le golden
   existant) puis après le changement, avec exactement les mêmes options.
   Le SVG étant du texte, un `git diff --no-index --stat a.svg b.svg` indique
   déjà si quelque chose a bougé ; pour regarder le rendu, demander à
   l'utilisateur d'ouvrir le fichier (navigateur) ou le convertir en PNG si
   un outil est disponible, puis le lire avec `Read`.
3. Pour `digitize`, compléter avec `stats` sur le DST : nombre de sauts,
   de changements de couleur, dimensions en mm — les régressions
   d'auto-numérisation se voient souvent là avant de se voir à l'œil.
4. **Transformer la repro en test Catch2** (même forme, mêmes paramètres,
   assertions sur des grandeurs mesurables : couverture, nombre de sections,
   longueur max de point, absence de saut…) avant de corriger — voir
   `openstitch-bugfix`.

## Régénérer les SVG dorés (`tests/golden/`)

Les tests ne réécrivent **jamais** ces fichiers. Les régénérer seulement
quand un changement de rendu est **voulu** et validé :

- `tests/golden/auto-satin/` : `columns-<forme>.svg`, `lot3-*.svg`… →
  `auto-satin-debug --shape <forme> <options du lot> --output-svg tests/golden/auto-satin/<fichier>.svg`
- `tests/golden/stitch-generation/` → `stitchdebug … --output-svg …`

Retrouver les options exactes d'un golden avant de l'écraser :
`openstitch-docs` (« golden », nom du fichier) — `docs/source/testing.md`
et `satin.md` les listent. Ne régénérer que les fichiers concernés, et
signaler dans le message de commit quels golden ont changé et pourquoi.

## Ajouter une forme ou une option de debug

Tout est dans `apps/cli/main.cpp` (formes de référence construites en µm,
options CLI11 déclarées dans `main()`). Une nouvelle forme ajoutée pour un
bug doit aussi être listée dans le texte d'aide de `--shape`. Tout appel à
`generate_sequence` sur un projet synthétique doit porter l'annotation
`raw-sequence-ok: <raison>` ; sur un vrai projet, utiliser
`effective_sequence`.
