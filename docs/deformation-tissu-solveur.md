# Déformation prévue du tissu — solveur 2D de la feuille

Date : 2026-11-01. Complète `theorie-tirage-tissu-2026-11.md` (noyau `PullField`, déplacement des
POINTS) et `fabric-risk-heuristic.md` (indice heuristique). **Estimation non calibrée** : rigidités,
tension du fil et seuil de plissement sont des ordres de grandeur empiriques (`calibrated = false`).

## Ce que le modèle fait

La feuille tissu + stabilisateur est un problème aux limites : élasticité plane **orthotrope**
(rigidités de membrane chaîne / trame en N/mm, cisaillement, coefficient de Poisson, sens du grain),
bord **fixé** au cadre (canevas du projet), maillage carré Q4. Chaque segment cousu (séquence
effective, `collect_pull_sources`) applique **deux forces de tension égales dirigées l'une vers
l'autre** aux pénétrations, étalées en gaussienne (`load_radius_mm`, évite la singularité et rend le
résultat indépendant du maillage). Le fil raidit localement la feuille (`1 + stitch_stiffening ×
densité de fil`). On résout K u = f par gradient conjugué préconditionné (Jacobi), déterministe
(ordre fixe, un seul fil, itérations bornées).

Sorties (`libs/stitch_analysis/sheet_deformation.hpp`) : déplacement aux noeuds (mm), compression
principale par élément, **indice de plissement** = compression / `critical_compressive_strain`
(1 = seuil atteint), aire au-dessus du seuil, pics et position.

## Ce qu'il ne fait pas

- **Pas de flambage hors plan** : les plis ne sont pas simulés ; l'indice dit où la compression dans
  le plan dépasse un seuil critique.
- **Régime quasi statique, état final** : l'ordre de couture n'intervient pas ici (il est dans
  `PullField`). Pas de fluage ni de relaxation à la sortie du cerceau.
- **Linéaire** : le seul effet non linéaire est le raidissement par la densité de fil.
- **Cadre fixe** : un cerclage réel n'est ni parfaitement rigide ni sans pré-tension.
- Réglages « woven / jersey / felt » × « none / tearaway / cutaway » : empiriques.

## Vérifications (tests)

`tests/unit/stitch_analysis/test_sheet_deformation.cpp` : bord fixé nul, symétries d'un point isolé,
linéarité en tension et proportionnalité inverse à la rigidité, rotation du grain de 90°, trame plus
souple => déplacement plus grand, raidissement => déplacement plus petit, seuil critique /2 => indice
×2, déterminisme. `tests/unit/project_ops/test_analysis.cpp` : entoilage rigide réduit déplacement et
plissement, jersey cède plus, JSON/PNG déterministes.

## Utilisation

- CLI : `openstitch-cli project fabric-deformation projet.osp [--material jersey] [--stabilizer cutaway]
  [--heatmap carte.png] [--json]` (schéma `openstitch.fabric-deformation/1`).
- Bureau : `Analyse > Déformation prévue du tissu…` (rapport + carte de l'indice). Tissu, entoilage,
  sens du grain et souplesse de trame viennent du projet (« Coloris et tissu »).

## Calibrer (HP-PHYS-001)

La pièce-test (`pull-test-piece`) donne des mesures de rétrécissement de colonnes, de jour
remplissage/contour et de croix de repérage. Il faut y ajouter des mesures de **plis** (hauteur ou
présence) sur des aplats de taille croissante, et en déduire `stiffness_*`, `thread_tension_n`,
`critical_compressive_strain`. Tant que ce n'est pas fait, comparer des versions d'un motif, ne pas
lire les valeurs comme des mesures.

## Reste

Surimpression de l'indice directement sur le canevas (cache comme le « Risque tissu »), couplage avec
`PullField` (ordre de couture), cadre élastique / pré-tension, table de matériaux persistante dans le
`.osp`, MCP.
