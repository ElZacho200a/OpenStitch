# Remplissage tatami

Public : utilisateur avancé, développeur.

> État : Présent dans le code : oui · Tests unitaires : oui (invariants de
> routage) · Tests visuels : partiels (SVG de diagnostic) · Import/export DST :
> oui · Test sur machine réelle : **non** · **Statut recommandé : partiel**
> (scanline + routage validé géométriquement ; **sous-couches, underpath caché et
> entrée** ajoutés au Lot 7 ; motifs de phase avancés non implémentés).

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
(`connector_invalid`) : le connecteur `[a,b]` est **découpé** à chaque
intersection paramétrique avec une arête (extérieur ou trou) — y compris un
contact **dégénéré par un sommet** (le connecteur touche exactement un coin
sans « croiser » franchement une arête) — puis le **milieu de chaque
sous-segment** ainsi obtenu doit rester dans la région :

- une arête **colinéaire** au connecteur (suivi de bord/frontière) ne produit
  aucune découpe → le suivi de bord reste cousu, quelle que soit son
  orientation ;
- toute autre intersection (croisement franc, **ou simple contact par un
  sommet**) découpe le connecteur ; si un sous-segment tombe hors de la région
  → saut. Ce test est **indépendant de l'écart en x** du connecteur : un
  connecteur **parfaitement vertical** qui traverse un trou de part en part en
  touchant exactement ses deux sommets (haut et bas) est donc détecté, alors
  qu'une version antérieure de `connector_invalid` — qui excluait les contacts
  sommet/extrémité du test de croisement **et** ne sondait l'intérieur que si
  l'écart en x dépassait `2 × row_spacing` — le laissait passer à tort (corrigé
  courant Lot 7, cf. `segment_stays_in_region` pour la validation exposée).

En l'absence de trajet intérieur valide, le moteur utilise un **saut**. Chaque
point est un `FillStitch { pos, jump }` (`jump = true` = aiguille levée). Ainsi
aucune couture ne traverse un trou ni ne sort de la région.

Note : Ce comportement corrige plusieurs défauts successifs signalés en revue
(les remplissages débordaient sur les régions à trou ; puis la justification
« chevauchement ⇒ liaison intérieure » était trop optimiste ; puis les contacts
par sommet échappaient à la validation géométrique). Vérification via
`openstitch-cli stitchdebug --shape ring` : sur un anneau, 0 couture traverse le
trou et le contournement ne coûte que ~2 sauts. Des tests couvrent aussi une
forme concave en **U** (aucune couture ne traverse l'encoche), une forme en
**L** — où l'on échantillonne chaque segment cousu à **cinq angles**
(0/20/45/90/135°) pour vérifier qu'**aucun ne sort de la région** (le test
tolère les coutures qui *longent* le bord) et où l'on vérifie spécifiquement
qu'un contact au **coin rentrant** ne laisse pas passer une couture vers
l'encoche —, et des trous en **losange** (sommets alignés verticalement,
pluralité de trous) qui exercent précisément le cas des contacts sommet. C'est
ce filet qui garantit que router les zones vers le tatami — plutôt que vers le
satin naïf — supprime le débordement (voir *Colonne satin*).

Point subtil de `in_region` : le test pair-impair est ambigu **exactement sur
une frontière**. Pour un point sur le bord de l'**extérieur**, il se résout
correctement en « intérieur » ; pour un point sur le bord d'un **trou**, une
implémentation naïve le classerait à tort « dans le trou » (parité déclenchée
par l'arête opposée du trou), ce qui rejetterait un suivi de bord pourtant
légitime. `in_region` traite donc explicitement tout point **sur** une
frontière (distance quasi nulle à une arête, tolérance ~0,01 µm — une marge
numérique, pas géométrique) comme faisant partie de la région, avant le test
pair-impair.

Choix de l'auto-numérisation : comme le satin naïf déborde, **toute zone
remplissable est désormais numérisée en tatami** par défaut (fines bandes
comprises). Le tatami est le remplissage sûr tant que le vrai moteur satin
(squelette) n'est pas branché.

## Tatami avancé (Lot 7)

*État : Présent · Testé numériquement · Validé visuellement (SVG).* Tout est
**désactivé par défaut**, éditable (inspecteur) et persisté (`.osp`,
rétrocompatible). Chaque commande porte une `StitchPass` (§4) : sous-couches
`Underlay`, couche supérieure `TopStitch`, trajet caché `Travel`.

**Sous-couches** (`tatami_underlay`), cousues **avant** la couche supérieure :

- **Contour rentré** (`underlay_edge`) : running le long du bord **extérieur**,
  retrait `underlay_inset`. Les **bords de trous ne sont pas** longés (l'inset
  d'un trou le rétrécit vers son centre ; en longer le bord ferait passer la
  sous-couche au-dessus du trou — sûreté). **Politique sûre explicite** si le
  retrait échoue ou fait disparaître la forme (pièce trop petite pour
  `underlay_inset`) : **aucune** sous-couche de contour n'est émise pour cette
  forme — jamais de repli silencieux sur le bord **brut** (coudre exactement
  sur l'arête finale n'offre aucune marge de stabilisation et peut déborder une
  fois la compensation de bord de la couche supérieure appliquée par-dessus).
  Un retrait **explicitement nul** (`underlay_inset` = 0) est une intention
  distincte, pas un échec : le bord brut est alors bien celui voulu.
- **Rangées parallèles espacées** (`underlay_parallel`) : balayage tatami
  **perpendiculaire** aux rangées supérieures, pas `underlay_spacing`. Réutilise
  le routage (contourne les trous). Évite l'affaissement de la surface.

**Underpath caché** (`hidden_underpath`) : au lieu de sauter, une liaison est
**cousue et cachée** quand un trajet valide existe — soit **direct** (s'il reste
intérieur et court), soit **le long du contour extérieur rentré** (« autoroute »
qui **contourne les trous**). Les pénétrations intermédiaires sont taguées
`Travel`. Au-delà d'un plafond de longueur (`max(6·pas, 8 mm)`), on **saute**
(un déplacement à découvert long serait pire qu'une coupe). Toujours borné par la
validation géométrique : **jamais** de trajet caché à travers un trou.

**Entrée** (`entry_point`, optionnel) : le remplissage démarre par le segment le
plus proche du point d'entrée, puis chaque nouvelle composante est atteinte par
le segment non visité le plus proche.

Vérifié : sous-couche de contour rentrée dans la forme ; sous-couche parallèle
espacée ; underpath convertit au moins un saut en trajet cousu **sans jamais**
traverser le trou (invariant préservé) ; déterminisme ; l'entrée oriente le
démarrage ; aller-retour `.osp` ; retrait de contour impossible (pièce trop
petite) → aucune sous-couche émise (pas de repli sur le bord brut) ; retrait
explicitement nul → bord brut (intention voulue). SVG :
`openstitch-cli stitchdebug --shape ring --underlay 3 --underpath` (contour et
parallèle en vert, couche supérieure en gris, underpath en bleu, sauts en rouge).

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
| `underlay_edge` | bool | off | sous-couche de contour (Lot 7) | — |
| `underlay_inset` | mm | 0,6 | contour de sous-couche plus rentré (échec/disparition → aucune sous-couche émise, jamais le bord brut ; 0 = bord brut voulu) | contour plus proche du bord final |
| `underlay_parallel` | bool | off | sous-couche perpendiculaire espacée (Lot 7) | — |
| `underlay_spacing` | mm | 2,0 | rangées de sous-couche plus espacées | plus denses |
| `hidden_underpath` | bool | off | coud et cache les liaisons courtes (Lot 7) | plus de sauts |
| `entry_point` | µm | — | démarre le remplissage près de ce point (Lot 7) | — |

Le paramètre principal de **densité** est `row_spacing` (entre rangées) — à ne
pas confondre avec `stitch_length` (le long d'une rangée). Le retrait de bord
est appliqué **en amont** (offset intérieur du polygone) par `generate_tatami`.
Tous ces paramètres, y compris `underlay_inset` et `underlay_spacing`, sont
éditables dans l'inspecteur (`PropertiesPanel`), avec undo/redo générique
(`SetStitchParamsCommand`).

### Orientation éditable (angle des fils)

L'`angle` se règle à la création, puis se **modifie après coup** : soit
numériquement (menu *Broderie ▸ Orientation du remplissage…*, ou clic droit sur
la forme), soit **directement dans la scène** en faisant glisser une **poignée de
rotation** (un axe bleu au centre du remplissage). Le nouvel angle est appliqué
au relâchement via une commande annulable (`SetFillAngleCommand`) ; les points
sont régénérés (ADR-014). L'angle est modulo 180° (orientation d'une droite).

> Validation physique : non effectuée. Ces valeurs sont des points de départ
> logiciels, pas des réglages éprouvés sur machine.

Limitation réelle (au-delà du Lot 7, cf. ci-dessus qui **est** implémenté) :
les **motifs de phase avancés** (staggering non uniforme, densité variable)
restent **prévus, non implémentés**.

## Implémentation associée

- `libs/document/.../embroidery_object.hpp` — `TatamiParams`.
- `libs/stitch_generation/include/openstitch/stitch_generation/tatami.hpp` —
  `FillStitch`, `fill_tatami`, `tatami_underlay`, `segment_stays_in_region`
  (validation géométrique exposée pour test).
- `libs/stitch_generation/src/tatami.cpp` — scanline, graphe de routage,
  `connector_invalid` (validation géométrique des trajets cousus, découpe
  paramétrique robuste aux contacts sommet), `in_region` (frontière traitée
  comme intérieure, tolérance numérique).
- `libs/stitch_generation/src/generate.cpp` — `generate_tatami`, `emit_fill`.
- `libs/geometry/src/offset.cpp` — `inset_path_set` (retrait de bord).
- `apps/desktop/properties_panel.cpp` — inspecteur : tous les champs
  `TatamiParams`, y compris `underlay_inset`/`underlay_spacing`.
- Tests : `tests/unit/stitch/test_tatami.cpp` (invariants : aucune couture dans
  le trou ; aucune couture hors région sur une forme en L à tous les angles ;
  contacts sommet — trou en losange, multi-trous, coin rentrant d'un L ;
  politique sûre de `tatami_underlay` sur retrait impossible/nul),
  `tests/unit/geometry/test_offset.cpp`.
- `libs/commands/.../project_commands.hpp` — `SetFillAngleCommand` (orientation),
  `ConvertFillsToTatamiCommand`, `SetStitchTypeCommand`.
