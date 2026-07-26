# Remplissage tatami

Public : utilisateur avancé, développeur. État : **Implémenté** (routage par
graphe) ; sous-couche tatami **prévue**.

## Définition

Le tatami remplit une zone pleine par des **rangées parallèles** de points, avec
un décalage régulier des pénétrations d'une rangée à l'autre pour éviter un
sillon visible. C'est le remplissage de référence pour les surfaces larges.

## Génération par balayage (scanline)

`fill_tatami(region, params)` :

1. la géométrie (extérieur + trous) est virtuellement **tournée** de `-angle`
   pour travailler avec des rangées horizontales ;
2. pour chaque rangée `y`, on calcule les **intersections** avec tous les
   contours et on forme les **segments intérieurs** par paires (règle
   pair-impair) — une rangée peut donc comporter **plusieurs segments** (formes
   concaves, trous) ;
3. sur chaque segment, les pénétrations sont posées sur une **grille** de pas
   `stitch_length`, décalée d'une rangée à l'autre selon `stagger` ;
4. la géométrie est retournée dans le repère d'origine.

## Routage qui contourne les trous

C'est le point délicat. Les segments de rangée sont les **nœuds d'un graphe** :
deux segments de rangées voisines qui **se chevauchent en x** sont adjacents (la
bande entre eux est intérieure, donc une liaison ne peut pas traverser un trou —
le trou aurait coupé la rangée). Un **parcours glouton** suit toujours une arête
du graphe (couture qui reste dans la région) et ne fait un **déplacement** que
vers un segment non adjacent.

Chaque point de remplissage est un `FillStitch { pos, travel }` : `travel = true`
signale un déplacement (aiguille relevée). Ainsi **aucune couture ne traverse un
trou ni ne sort de la région**.

Note : Ce comportement corrige un défaut signalé par un utilisateur (les
remplissages débordaient sur les régions à trou). Vérification via
`openstitch-cli stitchdebug --shape ring` : sur un anneau, 0 couture traverse le
trou et le contournement ne coûte que ~2 déplacements.

## Paramètres

| Paramètre | Rôle |
|---|---|
| `row_spacing` | espacement entre rangées (densité) |
| `stitch_length` | longueur de point le long d'une rangée |
| `angle` | orientation des rangées |
| `inset` | retrait de bord (compensation de contour, offset Clipper2) |
| `stagger` | nombre de rangées avant répétition de la phase des pénétrations |

Le retrait de bord est appliqué **en amont** (offset intérieur du polygone) par
`generate_tatami`.

Limitation : la **sous-couche tatami**, l'**underpath caché** (déplacements
cousus sous la couche supérieure au lieu de sauts), les points d'**entrée/sortie**
imposés et les motifs de phase avancés sont **prévus**, non implémentés.

## Implémentation associée

- `libs/document/.../embroidery_object.hpp` — `TatamiParams`.
- `libs/stitch_generation/include/openstitch/stitch_generation/tatami.hpp` —
  `FillStitch`, `fill_tatami`.
- `libs/stitch_generation/src/tatami.cpp` — scanline + graphe de routage.
- `libs/stitch_generation/src/generate.cpp` — `generate_tatami`, `emit_fill`.
- `libs/geometry/src/offset.cpp` — `inset_path_set` (retrait de bord).
- Tests : `tests/unit/stitch/test_tatami.cpp` (invariant : aucune couture dans le
  trou), `tests/unit/geometry/test_offset.cpp`.
