# Remplissage directionnel

Public : utilisateur avancé, développeur. État : **Implémenté (Phases 1 à 3)** ·
Tests unitaires : oui · Tests visuels : non · Test sur machine réelle : **non** ·
**Statut recommandé : expérimental**.

## Objectif

Le tatami couse des rangées **droites** à angle fixe. Le remplissage
directionnel est une **alternative** (le tatami n'est pas modifié) dont les
points **suivent la forme**, pour imiter la broderie main — passé empiétant,
peinture à l'aiguille. Exemple de référence : une vague dont les points partent
de la crête et descendent en éventail en suivant le rouleau, avec des secteurs
aux directions différentes (chevrons) et des points de longueurs irrégulières
qui s'imbriquent.

## Modèle de données

`document::DirectionalFillParams`, quatrième alternative du variant
`StitchParams` (ajoutée **en fin** de variant : les index des types
historiques ne changent pas).

| Champ | Défaut | Rôle |
|---|---|---|
| `guides` | vide | courbes guides (polylignes ou Béziers, ouvertes), µm, repère du modèle |
| `break_lines` | vide | lignes de rupture (secteurs indépendants) |
| `row_spacing` | 0,4 mm | écart entre lignes de couture (densité) |
| `stitch_length` | 3 mm | longueur cible, **bornée à [1 ; 7] mm** à la génération |
| `edge_weight` | 0 | influence de la tangente du bord le plus proche, [0 ; 1] |
| `inset` | 0,2 mm | retrait de bord (comme le tatami) |
| `stagger` | 2 | lignes avant répétition de la phase des pénétrations |
| `underlay_edge` / `underlay_parallel` | non | sous-couches (générateurs du tatami) |
| `underlay_inset` / `underlay_spacing` | 0,6 / 2 mm | réglages des sous-couches |
| `hidden_underpath` | oui | liaisons cousues cachées plutôt que des sauts |
| `sector_overlap` | 0,25 mm | chevauchement le long des ruptures |
| `handmade` / `handmade_intensity` | non / 50 % | aspect fait main |
| `seed` | id de l'objet | graine de l'aspect fait main |

Les guides sont exprimés dans le **même repère que le contour** de l'objet
vectoriel source : `TranslateVectorObjectCommand` et
`ScaleVectorObjectCommand` les déplacent / redimensionnent avec la forme
(undo exact).

**Format `.osp`** : type `"directional"`, toutes les clés sauf `type` sont
optionnelles (défauts du modèle). `schemaVersion` n'a pas changé : un projet
ancien se relit à l'identique. Un projet contenant un remplissage directionnel
n'est pas lisible par une version antérieure (« Type de point inconnu »).

## Algorithme

`libs/stitch_generation/src/directional_fill.cpp`, fonctions pures et
déterministes. `generate_sequence` l'appelle comme le tatami : retrait de bord
(repli sur la forme brute si le retrait la fait disparaître), sous-couches,
puis couche supérieure.

### 1. Secteurs (Phase 2)

Chaque ligne de rupture est convertie en bandes de 20 µm (prolongées de
`max(espacement, 1 mm)` aux extrémités pour qu'une rupture tracée « presque »
jusqu'au bord la traverse quand même) et soustraite de la région
(`geometry::subtract_polygons`) : chaque composante est un **secteur**. Les
miettes (< 4 espacement²) sont ignorées. Chaque secteur a son propre champ,
ses propres lignes et sa propre grille de séparation ; il est tracé dans une
zone élargie de `sector_overlap` (offset puis intersection avec la région),
d'où le chevauchement qui évite les interstices le long des ruptures. Les
guides sont densifiés (pas ≤ 0,5 mm) puis **coupés** par secteur : un guide
qui traverse une rupture n'influence que ses deux morceaux respectifs.

Limitation : une rupture qui ne traverse pas la forme (ne la sépare pas) n'a
aucun effet — le champ s'interpole alors à travers elle.

### 2. Champ de directions

En chaque nœud d'une grille (pas = espacement, borné à [0,25 ; 1] mm) :

- pour chaque guide du secteur, point le plus proche et tangente de son
  segment ; pondération `1 / (d² + h²)` avec `h` = espacement (pas de
  singularité sur un guide) ;
- les tangentes sont moyennées sur l'**angle doublé** `(cos 2θ, sin 2θ)` : une
  orientation et son opposée donnent le même vecteur, le sens de tracé d'un
  guide est donc indifférent et deux guides quasi opposés ne s'annulent pas ;
- sans guide (ou en cas d'annulation exacte) : **axe principal** du secteur
  (`geometry::principal_axis`) ;
- influence du bord : `α = edge_weight · exp(−d_bord / bande)`, bande =
  `max(2 mm, 5 × espacement)`, mélange `(1 − α)·guides + α·tangente du bord`,
  toujours sur l'angle doublé.

Entre les nœuds, interpolation bilinéaire des vecteurs doublés.

### 3. Lignes de courant (Jobard & Lefer)

Algorithme de *Creating Evenly-Spaced Streamlines of Arbitrary Density*
(Jobard & Lefer, 1997) :

- distance de séparation `d_sep` = espacement, distance de test
  `d_test` = 0,5 × espacement ;
- intégration RK2 à pas fixe 0,2 × `d_sep`, dans les deux sens depuis la
  graine, le sens étant maintenu d'un pas à l'autre ;
- arrêt : sortie de la zone ou entrée dans un trou (le point de bord exact est
  retrouvé par dichotomie, pour que la couture atteigne le contour), approche
  à moins de `d_test` d'une autre ligne (ou d'elle-même hors d'une fenêtre de
  3 `d_sep`), virage > 40° en un pas (point singulier du champ) ;
- propagation : graines candidates à ± `d_sep` perpendiculairement à chaque
  ligne, tous les `d_sep`/2 ; file FIFO ;
- **comblement** : balayage final d'une grille au pas `d_sep`/4 ; tout point
  resté à plus de 0,9 `d_sep` de toute ligne devient une graine — aucune zone
  vide ni îlot oublié.

Chaque ligne porte une coordonnée d'arc `u` héritée de sa ligne mère (même
valeur au point de la graine) et un **rang** (± 1 par rapport à la mère) : ce
sont eux qui alignent les pénétrations d'une ligne à sa voisine.

Structures d'accélération : `RegionIndex` (grille dont chaque case connaît
ses arêtes et l'état intérieur/extérieur de son centre — point intérieur,
segment intérieur et bord le plus proche en temps quasi constant) et
`SeparationGrid` (cases de `d_sep`).

### 4. Découpe en points

- Pénétrations sur une grille de pas `L` (longueur cible bornée à
  [1 ; 7] mm) en coordonnée `u`, déphasée de `L · (rang mod stagger) / stagger`
  — effet brique comme le tatami ;
- aucun point < 0,5 mm aux extrémités ; aucun > 7 mm (scission en parts
  égales) ;
- une corde dont la flèche dépasse 0,15 mm, ou qui sortirait de la zone (bord
  concave), est scindée en deux (fidélité aux courbes).

### 5. Parcours

Par secteur, d'un seul tenant ; secteur suivant = celui dont une extrémité est
la plus proche. Dans un secteur, choix glouton de la ligne suivante :

1. parmi les liaisons **cousables** (≤ `max(4 × espacement, 1,5 mm)` et
   intérieures), règle de **Warnsdorff** : la ligne qui a le moins de voisines
   libres d'abord — une ligne courte coincée est cousue au passage au lieu de
   rester orpheline ;
2. sinon, l'extrémité la plus proche.

Le départ est une ligne **extrême** du faisceau (le moins de voisines). Le sens
de chaque ligne suit l'extrémité la plus proche : une ligne sur deux est
parcourue en sens inverse. Liaisons : point cousu si cousable ; sinon, avec
`hidden_underpath`, trajet caché direct (≤ `max(6 × espacement, 8 mm)`) ou le
long du contour extérieur rentré (mêmes règles que le tatami) ; sinon saut.

### 6. Sous-couches

`directional_underlay` réutilise `tatami_underlay` : contour rentré, et
rangées droites perpendiculaires à la **direction moyenne** du champ (moyenne
des vecteurs doublés sur une grille intérieure).

### 7. Aspect fait main (Phase 3)

Intensité `I` = `handmade_intensity` / 100 (0 si `handmade` est faux ; à
`I` = 0 le résultat est identique au mode régulier) :

- chaque pénétration bouge de ± 20 % × `I` × `L` : l'écart entre deux
  pénétrations varie de ± 40 % × `I`, puis est reborné à [1 ; 7] mm ;
- alternance court/long des **premier et dernier** points de chaque ligne
  selon la parité du rang (court = `(1 − 0,6 I) · L`) : les extrémités
  s'imbriquent au lieu de s'aligner ;
- ondulation de direction ≤ 6° × `I`, **bruit lissé** (valeurs tirées aux
  nœuds d'un réseau de 5 mm, interpolation smoothstep) — jamais aléatoire
  point par point ;
- tout tirage passe par un hachage splitmix64 de (graine, clés) — jamais
  `std::uniform_*_distribution`, dont la sortie dépend de la bibliothèque
  standard : même projet, même résultat sur toute plateforme.

## Interface

- **Conversion** : bouton « Convertir en remplissage directionnel » de
  l'inspecteur d'un tatami, ou « Type de points ▸ Remplissage directionnel »
  (menu contextuel, barre contextuelle). Les réglages du tatami sont repris et
  un guide droit à son angle, traversant la forme, reproduit l'orientation
  initiale (`directional_from_tatami`). `ConvertFillGroupCommand`, annulable.
- **Inspecteur** : espacement, longueur (1 à 7 mm), influence des bords,
  retrait, décalage, chevauchement des secteurs, sous-couches, liaisons
  cachées, aspect fait main (intensité, « Autre tirage » = nouvelle graine
  déterministe), résumé du nombre de guides et de ruptures.
- **Mode « Guides de direction »** (Broderie ▸ Guides de direction…, touche
  D, ou « Éditer les guides… » dans l'inspecteur) : affiche l'aperçu du champ
  (petits traits de direction, avant génération), les guides (traits pleins),
  les ruptures (tirets) et une poignée par point. Glisser une poignée déplace
  le point ; clic droit : supprimer le point ou le guide entier. « Tracer un
  guide de direction » / « Tracer une ligne de rupture » : clics successifs,
  Entrée / double-clic / ✓ pour terminer, Retour arrière pour retirer le
  dernier point, Échap pour annuler. Un guide devient une courbe lisse passant
  par les points cliqués (`geometry::smooth_open_path`, Catmull-Rom → Béziers) ;
  une rupture reste une polyligne (angles vifs des chevrons).
- Toute édition passe par `EditDirectionalFillCommand` (ou
  `SetStitchParamsCommand` depuis l'inspecteur) : undo/redo exact.

## Tests

`tests/unit/stitch/test_directional_fill.cpp` :

- guide horizontal → même densité qu'un tatami à 0° (± 5 %) ;
- guide en arc → toutes les cordes à moins de 10° du champ ;
- guides convergents → aucune paire de lignes à moins de 0,5 × espacement,
  aucune zone vide de plus de 2 × espacement ;
- déterminisme octet pour octet de `generate_sequence` ;
- repli sur l'axe principal, influence du bord, trous évités, bornes de
  longueur, conversion depuis le tatami, sous-couche, parcours sans saut sur
  forme convexe ;
- secteurs : champ discontinu de part et d'autre de la rupture, lignes
  cantonnées à leur secteur au chevauchement près, chevauchement effectif,
  un seul passage d'un secteur à l'autre ;
- fait main : identique au régulier à 0 %, longueurs variées dans [1 ; 7] mm,
  alternance court/long, dépendance à la seule graine, ondulation ≤ 6° et
  lisse.

Aussi : `test_roundtrip.cpp` (sérialisation), `test_undo_stack.cpp`
(translation/mise à l'échelle des guides, conversion), `test_primitives.cpp`
(`smooth_open_path`), `test_properties_panel.cpp` et `test_main_window.cpp`
(conversion, tracé de guides et de ruptures, undo/redo, sortie du mode).

## Limites connues

- Parcours : une forme dont les lignes se scindent en branches (fond en U,
  coins d'un éventail) impose une liaison entre branches ; au-delà du plafond
  du trajet caché (8 mm), c'est un saut — comme le tatami. Dans la bande de
  chevauchement d'un secteur, de courtes lignes peuvent encore être rejointes
  en fin de secteur par un saut.
- Les trajets cachés passent parfois sur une zone déjà cousue (même limite que
  le tatami).
- Pas d'édition des poignées de tangente des guides (seulement les points).
- Aucune validation sur machine réelle.
- Phase 4 (fondu de couleurs) : non implémentée.

## Implémentation associée

- `libs/document/include/openstitch/document/embroidery_object.hpp` —
  `DirectionalFillParams`.
- `libs/stitch_generation/{include/.../directional_fill.hpp, src/directional_fill.cpp}`.
- `libs/stitch_generation/src/generate.cpp` — `generate_directional`.
- `libs/project_io/src/json_serialize.cpp` — type `"directional"`.
- `libs/commands/include/openstitch/commands/project_commands.hpp` —
  `EditDirectionalFillCommand`, guides dans Translate/Scale.
- `libs/geometry/src/primitives.cpp` — `smooth_open_path`.
- `apps/desktop/main_window_directional.cpp`, `properties_panel.cpp`.
