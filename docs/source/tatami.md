# Remplissage tatami

Public : utilisateur avancé, développeur.

> État : Présent dans le code : oui · Tests unitaires : oui (invariants de
> routage) · Tests visuels : partiels (SVG de diagnostic) · Import/export DST :
> oui · Test sur machine réelle : **non** · **Statut recommandé : partiel**
> (scanline + routage validé géométriquement ; sous-couche, underpath caché,
> entrée/sortie et motifs de phase avancés non implémentés).

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
deux segments de rangées voisines qui **se chevauchent en x** sont considérés
comme **candidats** à une liaison. Un **parcours glouton** suit ces candidats et
ne saute (aiguille levée) que vers un segment non relié.

Le chevauchement n'est **qu'une heuristique d'adjacence** : il ne prouve pas
qu'une liaison entre les extrémités des deux segments reste dans la région. Une
corde entre deux points posés sur les bords peut longer l'extérieur (encoche,
poche concave d'une forme en U, pont au-dessus d'un trou). **Chaque trajet cousu
est donc validé géométriquement** contre la région et ses trous
(`connector_invalid`) :

- s'il **croise franchement** une arête (extérieur ou trou) → saut ;
- s'il s'agit d'un **connecteur diagonal large** dont un point intérieur sort de
  la région → saut ;
- les connecteurs **quasi-verticaux le long d'un bord** restent cousus.

En l'absence de trajet intérieur valide, le moteur utilise un **saut**. Chaque
point est un `FillStitch { pos, jump }` (`jump = true` = aiguille levée). Ainsi
aucune couture ne traverse un trou ni ne sort de la région.

Note : Ce comportement corrige deux défauts successifs signalés en revue (les
remplissages débordaient sur les régions à trou ; puis la justification
« chevauchement ⇒ liaison intérieure » était trop optimiste). Vérification via
`openstitch-cli stitchdebug --shape ring` : sur un anneau, 0 couture traverse le
trou et le contournement ne coûte que ~2 sauts. Des tests couvrent aussi une
forme concave en **U** (aucune couture ne traverse l'encoche) et une forme en
**L**, où l'on échantillonne chaque segment cousu à **cinq angles** (0/20/45/90/135°)
pour vérifier qu'**aucun ne sort de la région** (le test tolère les coutures qui
*longent* le bord). C'est ce filet qui garantit que router les zones vers le
tatami — plutôt que vers le satin naïf — supprime le débordement (voir *Colonne
satin*).

Choix de l'auto-numérisation : comme le satin naïf déborde, **toute zone
remplissable est désormais numérisée en tatami** par défaut (fines bandes
comprises). Le tatami est le remplissage sûr tant que le vrai moteur satin
(squelette) n'est pas branché.

Limitation : `jump` désigne un **saut machine** (aiguille levée), pas un
véritable *travel stitch* cousu-caché (underpath). Le routage par déplacements
cachés sous la couche supérieure n'est pas implémenté.

![Tatami d'un anneau](../assets/generated/tatami-ring.svg)

*Figure — Trajectoire réelle (SVG de diagnostic) : anneau à trou central rempli
en tatami. Le remplissage contourne le trou ; aucune couture ne le traverse
(seulement ~2 sauts).*

## Paramètres

Valeurs par défaut lues dans `TatamiParams`
(`libs/document/.../embroidery_object.hpp`).

| Paramètre | Unité | Défaut | Effet quand on augmente | Effet quand on diminue |
|---|---|---|---|---|
| `row_spacing` (densité) | mm | 0,4 | remplissage plus léger, risque de jours | remplissage plus dense, plus rigide |
| `stitch_length` | mm | 3,0 | points plus longs le long des rangées | points plus courts, plus de pénétrations |
| `angle` | ° (rad interne) | 0 | oriente les rangées | — |
| `inset` (retrait de bord) | mm | 0,2 | remplissage plus rentré | remplissage plus au bord (risque de débord) |
| `stagger` | rangées | 2 | pénétrations décalées sur plus de rangées | sillon plus visible |

Le paramètre principal de **densité** est `row_spacing` (entre rangées) — à ne
pas confondre avec `stitch_length` (le long d'une rangée). Le retrait de bord
est appliqué **en amont** (offset intérieur du polygone) par `generate_tatami`.

### Orientation éditable (angle des fils)

L'`angle` se règle à la création, puis se **modifie après coup** : soit
numériquement (menu *Broderie ▸ Orientation du remplissage…*, ou clic droit sur
la forme), soit **directement dans la scène** en faisant glisser une **poignée de
rotation** (un axe bleu au centre du remplissage). Le nouvel angle est appliqué
au relâchement via une commande annulable (`SetFillAngleCommand`) ; les points
sont régénérés (ADR-014). L'angle est modulo 180° (orientation d'une droite).

> Validation physique : non effectuée. Ces valeurs sont des points de départ
> logiciels, pas des réglages éprouvés sur machine.

Limitation : la **sous-couche tatami**, l'**underpath caché** (déplacements
cousus sous la couche supérieure au lieu de sauts), les points d'**entrée/sortie**
imposés et les motifs de phase avancés sont **prévus**, non implémentés.

## Implémentation associée

- `libs/document/.../embroidery_object.hpp` — `TatamiParams`.
- `libs/stitch_generation/include/openstitch/stitch_generation/tatami.hpp` —
  `FillStitch`, `fill_tatami`.
- `libs/stitch_generation/src/tatami.cpp` — scanline, graphe de routage,
  `connector_invalid` (validation géométrique des trajets cousus).
- `libs/stitch_generation/src/generate.cpp` — `generate_tatami`, `emit_fill`.
- `libs/geometry/src/offset.cpp` — `inset_path_set` (retrait de bord).
- Tests : `tests/unit/stitch/test_tatami.cpp` (invariants : aucune couture dans
  le trou ; aucune couture hors région sur une forme en L à tous les angles),
  `tests/unit/geometry/test_offset.cpp`.
- `libs/commands/.../project_commands.hpp` — `SetFillAngleCommand` (orientation),
  `ConvertFillsToTatamiCommand`, `SetStitchTypeCommand`.
