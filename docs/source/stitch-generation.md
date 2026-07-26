# Génération de points — point droit et fondations

Public : utilisateur avancé, développeur. État : **Implémenté** (reconstruit sur
des fondations de longueur d'arc).

![Séquence de génération des points](../assets/generated/seq-stitchgen.svg)

*Figure — Génération de la séquence de points à partir du document.*

## Fondations géométriques

Avant tout type de point, deux primitives (dans `geometry`) :

- **Aplatissement de Bézier adaptatif** (`flatten`) : les segments à tangentes
  sont subdivisés récursivement (De Casteljau) jusqu'à ce que l'écart à la corde
  passe sous une tolérance. On n'échantillonne **jamais** une Bézier par pas
  uniforme du paramètre `t`.
- **Paramétrisation par longueur d'arc** (`cumulative_lengths`, `point_at_length`)
  et **ré-échantillonnage équilibré** (`resample_run`) : un tronçon de longueur
  L est divisé en `n = ceil(L / cible)` parts **égales**, donc chaque point est
  ≤ la longueur cible et il n'y a pas de segment résiduel minuscule.

## Point droit (running stitch)

`run_stitch(path, config)` :

1. aplatit le chemin (courbes comprises) ;
2. détecte les **coins vifs** (angle de rotation > seuil, ~35° par défaut) ;
3. découpe le chemin en tronçons entre coins ;
4. ré-échantillonne **chaque tronçon par longueur d'arc**.

Résultat : les coins sont des pénétrations **exactes** ; les portions lisses ont
un espacement **régulier**. Un cercle finement facetté ne produit donc plus un
point par facette, mais des points espacés de la longueur cible.

Le résultat est structuré : `RunningResult { points, warnings, stats }`. Il ne
plante jamais et renvoie un avertissement (chemin vide, trop court, point trop
long…).

## Chemins fermés, sens, phase, départ

`RunningConfig` expose : `reverse` (sens), `phase` (décalage curviligne des
boucles lisses) et `start` (point de départ projeté sur une boucle fermée). Une
boucle fermée revient à son point de départ sans doublon minuscule.

## Répétitions

`apply_repeat_mode` implémente des modes **explicites** :

| Mode | Effet |
|---|---|
| `SinglePass` | un passage |
| `BackAndForth` | aller complet puis retour (termine au départ) |
| `BeanStitch` | chaque intervalle cousu `n` fois (impair) — point triple |
| `Backstitch` | progression avec recouvrement |

Les mouvements nuls sont supprimés ; le nombre exact de traversées est testé.

## Outil de diagnostic

`openstitch-cli stitchdebug --shape circle|line|corner|bezier|star|ring
--length 3 --output-svg out.svg` génère les points sur une forme de référence,
affiche les statistiques (points, longueur, segment min/max) et un SVG. Utilisé
pour valider le moteur (voir *Tests*).

## Implémentation associée

- `libs/geometry/src/polyline.cpp` — `flatten`, longueur d'arc, `resample_run`.
- `libs/stitch_generation/src/running_stitch.cpp` — `run_stitch`,
  `apply_repeat_mode`, `apply_repeats`, `sample_path`.
- `apps/cli/main.cpp` — sous-commande `stitchdebug`.
- Docs de conception : `docs/stitch-engine-audit.md`,
  `docs/stitch-engine-research.md`.
- Tests : `tests/unit/geometry/test_polyline.cpp`,
  `tests/unit/stitch/test_running_stitch.cpp`.
