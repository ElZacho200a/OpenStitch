# Numérisation automatique

Public : utilisateur avancé, développeur. État : **Partiellement implémenté**
(heuristiques, non validées sur machine réelle).

La numérisation automatique (`autodigitize::auto_digitize`) transforme une
segmentation en objets de broderie **éditables** : un objet vectoriel par
région, puis un objet de broderie dont le type (satin, tatami, contour) est
choisi selon la forme (voir *Objets de broderie*). Ce chapitre documente les
réglages qui décident de la qualité du résultat cousu, introduits après
l'audit d'un cas réel (septembre 2026, ci-dessous).

## Cas d'étude : la marine plein cadre

Une image « carte de segmentation » (ciel, nuages, lune, mer en trois tons,
reflet de lune, rivage, deux bateaux, mouettes ; PNG sans alpha, environ
250 × 176 mm) numérisée puis exportée en DST donnait : 38 907 points, aucun
point sur le ciel, tous les tatami à 0° sans sous-couche, des interstices
entre remplissages voisins, 2 916 déplacements dont 1 321 de plus de 3 mm et
aucune coupe, 4 946 points de moins de 0,5 mm, le reflet éclaté en 817
morceaux et aucun point d'arrêt. Chaque section ci-dessous traite une de ces
causes (lots A à G de la mission).

## Fond présumé (Lot A)

Une image sans canal alpha n'a aucun pixel transparent : son fond devient une
région opaque comme les autres, souvent la plus grande.
`AutoOptions::skip_largest_region` exclut alors la **couleur** de la plus
grande région, et toutes les régions de cette couleur exacte (un fond peut
être fragmenté par le motif).

Avant ce lot, l'option était cochée d'office dès que l'image n'avait pas
d'alpha. Sur une image **plein cadre**, la plus grande région est un vrai
élément du motif (le ciel de la marine) : elle n'était pas brodée du tout.

La recommandation vient désormais de `segmentation::background_candidate(seg)`,
qui mesure pour la couleur de la plus grande région :

| Mesure | Sens |
|---|---|
| `area_ratio` | part de l'image couverte par cette couleur (toutes régions) |
| `lightness` | clarté CIELAB L* de la couleur (`cielab_lightness`, calcul analytique D65) |
| `sides_touched` | nombre de bords de l'image touchés (0 à 4) |
| `recommended` | `lightness > min_lightness` (90) **et** `sides_touched >= min_sides_touched` (3) |

Les seuils sont dans `BackgroundCandidateOptions`. Le dialogue *Numérisation
automatique* coche la case seulement si `recommended`, et affiche la pastille
de couleur, le pourcentage de surface, L* et le nombre de bords touchés. Le
CLI (`openstitch-cli digitize`) applique la même règle avec
`--skip-background -1` (défaut) et imprime la candidate ; `0`/`1` restent des
choix explicites prioritaires. Sur la marine, le ciel (bleu, L* ≈ 43) n'est
plus ignoré.

## Orientation et sous-couche des remplissages (Lot B)

Avant ce lot, chaque tatami créé par l'auto-numérisation recevait
`TatamiParams{}` : rangées à 0°, aucune sous-couche. Les valeurs par défaut de
`TatamiParams` **ne changent pas** (projets `.osp` existants, objets créés à
la main) : les réglages sont appliqués par une post-passe
(`configure_tatami_fills`) sur les tatami que l'auto-numérisation crée, y
compris les remplissages de repli d'un satin incomplet, pilotée par des
champs d'`AutoOptions`.

**Angle.** L'orientation des rangées suit l'axe principal de la surface de
l'objet (`geometry::principal_axis` : moments d'inertie du second ordre,
calculés exactement sur le polygone par la formule de Green, trous déduits).
Si le rapport des moments principaux est inférieur à `fill_isotropy_ratio`
(1,3), la forme est jugée quasi isotrope (disque, carré) : l'angle vaut
`isotropic_fill_angle` (45°).

**Voisins.** Deux tatami dont les régions se touchent (au sens de
`segmentation::region_adjacency`) ne doivent pas avoir des rangées à moins de
`min_neighbor_fill_angle_gap` (20°) l'une de l'autre, sinon la frontière
disparaît à l'œil. Les tatami sont traités par aire décroissante (puis
identifiant d'objet, pour un ordre total déterministe) : une grande région
fixe son angle naturel, une plus petite en conflit cherche l'angle le plus
proche du sien (pas de `fill_angle_search_step`, 5°) qui respecte l'écart
avec tous ses voisins déjà placés ; à défaut, celui qui maximise l'écart
minimal. C'est donc toujours la plus petite qui cède.

**Sous-couche** selon l'aire nette de l'objet :

| Aire | Sous-couche |
|---|---|
| < `underlay_edge_min_area_mm2` (20 mm²) | aucune |
| 20 à `underlay_parallel_min_area_mm2` (100 mm²) | contour rentré (`underlay_edge`) |
| ≥ 100 mm² | contour + rangées perpendiculaires (`underlay_parallel`) |

`auto_fill_angle = false` et `auto_fill_underlay = false` rétablissent le
comportement historique. L'import SVG direct (`auto_digitize_vectors`) reçoit
l'angle naturel et la sous-couche, mais pas la règle de voisinage (aucune
segmentation, donc aucune adjacence connue) — limite documentée.

## Implémentation associée

- `libs/segmentation/include/openstitch/segmentation/segmentation.hpp` —
  `cielab_lightness`, `BackgroundCandidateOptions`, `BackgroundCandidate`,
  `background_candidate`.
- `libs/autodigitize/src/autodigitize.cpp` — `auto_digitize`,
  `AutoOptions::skip_largest_region`, `configure_tatami_fills` (Lot B).
- `libs/geometry/include/openstitch/geometry/moments.hpp` — `principal_axis`.
- `apps/desktop/main_window.cpp` — `MainWindow::autoDigitize` (dialogue).
- `apps/cli/main.cpp` — `run_digitize`.
- Tests : `tests/unit/segmentation/test_segmentation.cpp` (cas « fond
  blanc », « plein cadre coloré », « blanc central », « deux bords »),
  `tests/unit/desktop/test_main_window.cpp`
  (`autoDigitizeDialogDoesNotSkipColoredFullFrameRegion`,
  `autoDigitizeDialogSkipsNearWhiteFramingBackground`),
  `tests/unit/geometry/test_moments.cpp`, `tests/unit/autodigitize/test_autodigitize.cpp`
  (cas « Lot B »).
