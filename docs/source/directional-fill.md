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
| `density_gradient` | absent | dégradé de densité (voir ci-dessous) ; remplace `row_spacing` pour le tracé |
| `spacing_regularity` | 0 | régularité de l'espacement [0 ; 1] (voir *Régularisation*) ; 0 = tracé brut |
| `smooth_field` | faux | champ lissé (voir *Champ lissé*) ; faux = champ historique |
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

**Dégradé de densité** (`DensityGradient`, HP-STI-010) : l'écart entre lignes
varie **linéairement** le long de l'axe `from` → `to` (µm, repère du modèle),
de `spacing_from` à `spacing_to`, constant au-delà des extrémités. Les écarts
sont bornés à **[0,1 ; 4] mm** à la génération ; un axe nul (`from == to`)
donne un écart uniforme `spacing_from`. Sans dégradé le tracé est identique
octet pour octet à l'ancien. L'axe suit la forme (déplacement, redimensionnement)
comme les guides ; les écarts, longueurs physiques, ne sont pas mis à l'échelle.
`row_spacing` reste la référence des seuils de parcours (liaisons cousables,
trajet caché), non du tracé.

**Format `.osp`** : type `"directional"`, toutes les clés sauf `type` sont
optionnelles (défauts du modèle) ; le dégradé est l'objet optionnel
`densityGradient` (`fromX`, `fromY`, `toX`, `toY`, `spacingFrom`, `spacingTo`,
µm entiers ; `curve` entier 0 linéaire / 1 douce / 2 marquée, absent = 0, valeur
inconnue relue comme 0). `schemaVersion` n'a pas changé : un projet
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
  `d_test` = 0,5 × espacement ; avec un dégradé de densité, ces deux distances
  sont **locales** (`d_sep(p)` évalué au point considéré) : un écart plus grand
  à un bout fait naître moins de lignes. Pas d'intégration `0,2 × d_sep_min`,
  fenêtre d'auto-proximité `3 × d_sep_max`, cellule de la grille =
  moyenne géométrique de `d_sep_min` et `d_sep_max` ;
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

### 3 bis. Régularisation de l'espacement (optionnelle)

Quand les directions convergent, Jobard & Lefer coupe les lignes qui se
rapprochent trop mais laisse un espacement irrégulier (mesuré sur un éventail
de deux guides : écart au plus proche voisin, dispersion 0,23 de l'écart
demandé). Avec `spacing_regularity > 0`, les sommets des lignes sont déplacés
par **une résolution linéaire creuse** (Liu et al., CGF 2023, §3.4), après le
tracé et avant la découpe en points. Énergie sans dimension (distances
divisées par l'écart local `s`) :

- **densité** : pour chaque sommet, son plus proche voisin d'une autre ligne de
  chaque côté (à peu près en face) doit se trouver à `±s` selon la normale du
  champ — l'ensemble des paires est la version « voisins » de l'arête de
  Delaunay de l'article, sans triangulation ;
- **direction** : chaque arête garde sa direction de tracé ;
- **étirement** : un sommet ne glisse pas le long de sa ligne (poids fort) ;
- **rappel** (poids 0,1) vers la position tracée.

Le curseur `spacing_regularity` règle le poids de densité de 0,1 à 2,5 face au
rappel fixe. Résolution par gradient conjugué préconditionné (Jacobi), 400
itérations au plus, mono-thread : déterministe. Garde-fous : déplacement
plafonné à `s/2` et sommet ramené (dichotomie) dans la zone de traçage s'il en
sortait. Le nombre de lignes ne change pas.

**Calibrage mesuré** (éventail de deux guides, 30 × 20 mm, écart 0,4 mm) :

| `spacing_regularity` | dispersion de l'écart | écart à la direction |
|---|---|---|
| 0 (brut) | 0,232 | 0,2° |
| 0,5 | 0,158 | 1,9° |

Au-delà, trop de sommets butent sur le plafond de déplacement et la géométrie
se dégrade : l'optimum est vers 0,5. Coût : proportionnel au nombre de sommets
(synchrone, comme le reste de la génération) ; réservé aux objets qui en ont
besoin.

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

## Champ lissé (`smooth_field`, 2026-11-23)

Le champ historique prend, en chaque nœud de la grille, la tangente des guides
pondérée par `1 / (d² + h²)` (h = écart des lignes). Sur une forme large ou
courbe (cœur, spirale, C épais) les guides voisins se partagent la grille en
facettes quasi Voronoï : la direction saute à leur frontière et les lignes
forment des coudes ou des éventails.

Avec `smooth_field`, le champ historique `F0` est adouci par une équation de
**Poisson écrantée** : chaque nœud libre minimise `|F − F0|² + σ²|∇F|²`
(vecteurs à angle doublé, donc sans orientation). Les nœuds situés à moins de
0,75 case d'un guide restent EXACTEMENT sur sa tangente ; ailleurs seules les
discontinuités sont adoucies, sur une longueur `σ = 0,5 × demi-largeur
maximale` (bornée à 1,2–8 mm). Une bande étroite garde donc son champ, une
forme large perd ses facettes.

Le système est résolu par Gauss-Seidel sur-relaxé à balayage fixe (déterministe)
sur une grille grossière de pas ≈ σ/8, puis interpolé bilinéairement : environ
0,1 s ajoutées sur une forme de 250 mm. Sans guide il n'y a rien à lisser.

Banc de 12 formes courbes (`project check` / `project density`), lissage contre
historique : cœur couvert 0,950 → 0,966, sauts 16 → 12 ; spirale sauts 18 → 6 ;
C épais couvert 0,959 → 0,975, sauts 20 → 11 ; banane 0,959 → 0,966. Régressions
connues : croissant (couvert 0,943 → 0,928) et corne (0,931 → 0,926) ; le S passe
de 7 à 9 sauts. La case est donc **désactivée par défaut**. Un recadrage des bouts
de guides (couper l'épine du squelette puis prolonger tout droit) a été essayé et
écarté : il dégrade le cœur, la spirale et l'onde.

## Alternatives écartées

**Graines par divergence** (sources et puits d'après la divergence de ρ·v,
arbre k-d, affectation puits ↔ ligne ; Liu et al., CGF 2023, §3.1–3.3). Elle
garantit le nombre de lignes de courant d'un champ de densité variable. Mesure
faite avant de la retenir : sur un dégradé de 20 mm (écart de 0,4 mm à 0,8,
1,6 ou 3 mm), le tracé Jobard & Lefer actuel donne **35, 22 et 15 lignes pour
34,7, 23,1 et 15,5 attendues** (intégrale de 1/s), soit moins de 5 % d'écart,
avec ou sans régularisation (test de non-régression
`directional density gradient keeps the row count at the integral of the
density`). Elle exigerait en plus un champ **orienté** (vecteurs, alors que le
champ est défini modulo 180°) et un solveur d'affectation. Coût et risque sans
gain mesurable : non implémentée. À reconsidérer si un cas de lignes qui
naissent ou meurent mal apparaît (champs très divergents).

**Parcours par arbre couvrant + profondeur d'abord** (même article, §3.5 :
arbre couvrant minimal sur les arêtes de Delaunay entre lignes, parcours qui
double chaque arête de l'arbre, d'où un fil continu sans saut). Mesure faite
avant de la retenir : sur des formes à branches (U, T, L, lignes horizontales
ou verticales) le parcours actuel — Warnsdorff plus liaisons cousues, trajet
caché le long du contour rentré — laisse **0 à 2 sauts, 10 à 22 mm au total**
(test `directional routing leaves at most two jumps on branching shapes`).
L'arbre ne supprimerait ces sauts qu'en recousant la même distance en retour
sur une zone déjà cousue (surépaisseur, fil visible en haute densité) : c'est
déjà ce que fait le trajet caché quand il est possible, avec un plafond de
8 mm voulu. Pas de gain mesurable : non implémenté.

## Grille partagée des fondus multicolores (2026-11-14)

**Problème mesuré** (audit du 14 novembre 2026, rectangle 30 × 20 mm, fil cousu par bande
rapporté à un remplissage uni) : chaque couche d'un fondu modulait sa propre densité et
traçait ses propres rangées. Rien ne garantissait qu'à un endroit donné les fils totalisent
100 % : de 73 % (trou visible au milieu d'un fondu à trois fils) à 191 % (profils différents
d'un fil à l'autre) ; en superposition, le dernier fil plafonnait à 30 % au-dessus d'un fond
plein, si bien que la couleur d'arrivée n'apparaissait jamais.

**Modèle** : `DensityGradient::shared_rows` et `DensityStop::lower`. Toutes les couches d'un
fondu reçoivent les mêmes paramètres de tracé (guide perpendiculaire à l'axe, écart nominal
uniforme, graine) : elles tracent donc **la même grille de rangées**. Chaque rangée reçoit un
seuil `u ∈ [0 ; 1)` : rang spatial le long de l'axe, puis suite de Weyl du nombre d'or (toute
fenêtre de rangées voisines répartit ses seuils presque uniformément). Un segment de points
appartient à la couche dont la tranche `[lower(t) ; lower(t) + w(t))` contient le seuil de sa
rangée, évaluée en son milieu (`SpacingField::owns`, `append_owned_runs`).

- **Complémentaire** : `lower_i = Σ_{j<i} w_j`, donc les tranches partitionnent `[0 ; 1]` à
  chaque position : chaque segment de rangée est cousu par **exactement un** fil. Couverture
  mesurée après correctif : 88 à 112 % par bande, quels que soient le nombre de fils et les
  profils (test `shared rows: complementary blends cover the shape like one plain fill`).
- **Superposition** : le premier fil est plein ; les suivants se partagent une grille de
  surcouche à 60 % de la densité nominale (`kOverlayDensity`), cousue par-dessus. Le dernier fil
  atteint ainsi ≈ 60 % de couverture à l'arrivée, total ≤ 160 %.
- **Profils** : `GradientThread::curve` façonne la transition qui mène à ce fil. Les poids
  `w_s = 1 − c(u)`, `w_{s+1} = c(u)` restent de somme 1 ; un profil non linéaire est
  échantillonné (8 arrêts par transition), jamais appliqué à `t` couche par couche.
- Radial et courbe : `t` varie le long d'une rangée, qui est alors découpée entre fils ;
  textures `Feathered`/`Stippled` : le bruit sur `t` rend les frontières irrégulières.
- Hachures croisées : grilles d'angles différents, sans partage ; elles reçoivent les mêmes
  poids (somme 1, surcouche à 60 %) mais pas la garantie de couverture.

Compatibilité : `shared_rows` absent (anciens projets) = ancien comportement, octet pour octet.
Toute modification d'un fondu le reconstruit sur la grille partagée.

## Parts lues dans l'image (2026-11-17)

Les profils d'un fondu multicolore venaient d'un seul paramètre géométrique `t` (axe,
rayon, spirale, distance au bord) : deux fils voisins au plus en un point, dans un ordre
fixé. Une zone de peinture (ciel de Turner, eau des *Nymphéas*) mélange librement 3 fils
ou plus : sur *Le Téméraire*, 62 % des pixels en demandent au moins trois (étude
`docs/fondus-picturaux-autodigitize.md`). Les parts de chaque fil peuvent désormais être
**lues dans une grille** en chaque point.

**Modèle.** `document::WeightGrid` : grille régulière (origine = centre de la case
(0, 0), pas, colonnes, rangées, canaux, valeurs sur 255), interpolée bilinéairement,
bornée au bord. L'ancre porte `MulticolorGradientParams::image_weights` : un canal par
fil, somme 255 par case. Chaque couche reçoit une grille à 2 canaux
(`DensityGradient::slice_grid`) : début et fin de **sa** tranche, sommes entières des
parts des fils précédents. La fin d'une couche est donc, en chaque case, exactement le
début de la suivante, et l'interpolation identique garde les tranches jointives au bit
près. La génération reste locale à chaque couche (`SpacingField::sampleSlice`).

**Deux façons de coudre** (`MulticolorGradientParams::complementary`) :

- *Grille partagée* (complémentaire, défaut) : toutes les couches tracent les mêmes
  rangées et chaque morceau de rangée est cousu par le seul fil dont la tranche contient
  le seuil de Weyl de la rangée (« Grille partagée » ci-dessus). Couverture exacte, mais
  un fil minoritaire ne possède qu'une rangée sur 1/part, en petits morceaux.
- *Rangées continues* (`complementary = false`), à la manière du Multi Blend de Hatch :
  chaque fil coud ses propres rangées, continues, d'écart `nominal / part` (plafonné à
  4 mm, coupées sous 2,5 %) ; les densités se somment à 1 en moyenne, sans coordination
  des rangées entre fils (quelques jours de tissu possibles).

Méthodes de remplissage seulement (directionnel, points longs et courts, semis) ; satin
entrelacé et hachures croisées refusent une grille. Une liaison cousue n'est permise que
là où la part du fil atteint 20 % (règle existante « ne pas traverser le creux d'un
fondu », appliquée à la part lue dans la grille).

**Calcul depuis l'image** (`autodigitize::image_blend_weights`, `libs/autodigitize/src/
image_blend.cpp`) : couleur de chaque case = moyenne en RVB **linéaire** des pixels de la
forme ; parts = moindres carrés sous contrainte (somme 1, parts ≥ 0, au plus
`max_threads_per_point` fils), résolus exactement sur tous les supports de 1 à 4 fils,
plus une pénalité de localité `λ·Σ wᵢ‖cᵢ − v‖²` (λ = 0,05) ; `min_share` retire les parts
trop faibles ; arrondi sur 255 par plus forts restes. `propose_blend_palette` choisit les
couleurs : candidates k-means et extrêmes de quatre axes de couleur (un mélange ne sort
pas de l'enveloppe de ses fils), meilleure paire puis ajout glouton selon l'erreur de
**mélange**. Déterministe, parallèle par rangée de cases. Opération `image_blend`
(`project_ops::make_image_blend_command`, un pas d'annulation : création ou recalcul).

**Mesures** (*Le Téméraire*, 150 × 111 mm, une seule forme couvrant le tableau, 6 fils,
rangées horizontales ; `project check`) :

| Variante | Points | Coupes | Fil |
|---|---|---|---|
| Auto-numérisation actuelle, 8 couleurs | 33 815 | 292 | 66 m |
| Auto-numérisation actuelle, 12 couleurs | 35 428 | 332 | 67 m |
| Grille partagée, case 1 mm | 21 844 | 1 243 | 48 m |
| Rangées continues, case 1 mm | 21 225 | 957 | 47 m |
| Rangées continues, case 3 mm | 18 846 | 533 | 45 m |
| Rangées continues, case 3 mm, part ≥ 15 %, 3 fils au plus | 17 911 | 433 | 44 m |

Le fondu supprime les îlots et le tissu visible de la mosaïque d'aplats et coud moitié
moins de points, mais **coupait plus** que l'auto-numérisation actuelle et montre des
stries de couleurs alternées le long des rangées. Ne limiter qu'à 2 fils par point
aggrave les coupes (652) : le fil change plus souvent le long d'une rangée.

**Trajets cachés sous les couches suivantes (2026-11-17).** Un fil rare ne coud que des
bouts de rangées épars ; la liaison cousue est bornée à 2,4 mm et interdite au travers du
creux d'un fondu, donc chaque bout coûtait une coupe. Les tranches étant cumulées de
l'arrière vers l'avant, les couches suivantes couvrent en `p` la part `1 − fin(p)`
(`SpacingField::coveredAfter`), connue localement sans regarder les autres couches. Entre
deux bouts, la couche voyage désormais en point courant (12 mm au plus, dans la forme) si
cette part atteint **0,75** sur tout le trajet, échantillonné tous les 0,4 mm ; sur le
premier et le dernier millimètre, ses propres rangées comptent aussi (`1 − début(p)`), car
une rangée s'arrête là où la part du fil croise son seuil et la couverture des seules
couches suivantes y vaut souvent ~0,5. Le choix de la rangée suivante préfère une rangée
joignable par trajet caché à la plus proche qui imposerait un saut. La dernière couche,
que rien ne recouvre, n'en profite jamais. Seuls les fondus à grille de tranches sont
concernés : les autres remplissages sont inchangés (auto-numérisation 12 couleurs :
332 coupes avant comme après).

| Variante (même projet) | Coupes avant | Coupes après |
|---|---|---|
| Grille partagée, case 1 mm | 1 243 | 947 |
| Rangées continues, case 1 mm | 957 | 697 |
| Rangées continues, case 3 mm | 533 | 388 |
| Rangées continues, case 3 mm, part ≥ 15 %, 3 fils au plus | 433 | **318** |

Seuil de couverture balayé sur la dernière ligne : 0,9 → 402 coupes, 0,75 → 319, 0,6 →
302, 0,45 → 284. Sous 0,75 le trajet affleure davantage entre les rangées du dessus,
surtout si le fil caché contraste avec elles ; au rendu 24 px/mm, à 0,75, un seul court
trajet reste perceptible sur un carré de 18 mm. Le meilleur réglage coupe maintenant moins
que l'auto-numérisation actuelle à 12 couleurs (318 contre 332) avec moitié moins de
points (17 402 contre 35 428). Test : `image weights: a scattered back layer travels
hidden under later layers` (12 îlots au fil du fond : aucun saut entre îlots ; au fil du dessus :
≥ 6 coupes). Pistes restantes : découpage en zones (lot 2 de l'étude, moins de fils par zone),
regroupement spatial des seuils (stries). Aucun essai sur tissu.

## Fondu de couleurs à deux fils

Idée de Liu et al. (*Directionality-Aware Design of Embroidery Patterns*,
CGF 42(2), 2023, §4.3), reprise sans code tiers. Une région dont les couleurs
s'étalent entre deux teintes se coud en **deux passes** : un fond uni de
couleur s1, puis un remplissage de couleur s2 dont l'écart entre lignes varie.
Avec `b` la largeur visible du fil de s2 et `1/ρ` l'écart entre ses lignes,
une période montre `b` de s2 et `1/ρ − b` de s1 : la proportion de s2 est
`t = b·ρ`, donc **`écart = b / t`** (borné à [`min_spacing` ; `max_spacing`]).

`autodigitize::analyze_two_color_blend(samples, options)`
(`libs/autodigitize/src/two_color_blend.cpp`) :

1. ACP des couleurs en **CMY** (première composante, itération de la
   puissance, signe canonique) ; s1 et s2 sont les extrémités de l'axe après
   rejet de 2,5 % de valeurs aberrantes de chaque côté ;
2. s1 (fond) est la couleur la mieux représentée : si `t` moyen dépasse 0,5, les
   rôles s'échangent ;
3. plan `t(x, y)` par moindres carrés → un `DensityGradient` (axe + écarts)
   directement utilisable comme `DirectionalFillParams::density_gradient`
   du remplissage de dessus ; sous `min_ramp` (0,05) de variation, l'axe est
   nul et l'écart uniforme `b / t̄`.

Le choix des fils réels (`thread_palette::best_thread_pair`) et le lissage
préalable (`BilateralDenoiseOp`) complètent la chaîne :
`autodigitize::analyze_region_blend` lisse l'image de travail, échantillonne les
pixels sous la forme (`sample_region_colors`, au plus 20 000, repère du modèle),
analyse, puis choisit la paire de fils dans le nuancier fourni.

**Action de l'interface** (`MainWindow::applyTwoColorBlend`, menu Broderie,
barre contextuelle et inspecteur d'un objet tatami ou directionnel) : le dialogue
**Fondu de couleurs · deux fils** propose les couleurs du fond et du dessus,
l'angle, les écarts au début/à la fin (0,1 à 4 mm), l'inversion, le profil (**complémentaire** par défaut : rampes opposées sur les deux
couches ; ou fond uniforme) et un **aperçu des points** : séquence effective d'un
instantané du projet (`MainWindow::createColorBlend`, `blendLayersFor`), jamais le
document, avec les mêmes couches que celles qui seront créées (pas une simulation du
tissu). Création et aperçu partagent `make_color_blend_layers` ; les deux couches
sont liées (`SetColorBlendLinkCommand`). Il fonctionne **sans
image**, avec des couleurs manuelles. Avec une image, **Proposer les couleurs
depuis l'image** lance l'analyse hors du fil d'interface, avec le nuancier actif
du panneau Fils. La proposition reste modifiable et l'analyse ne crée rien.
Annuler ferme le dialogue sans toucher au document ni à l'historique.

Après validation, l'objet sélectionné
devient le **fond** (il garde son rang, donc il est cousu avant ; son dégradé
éventuel est retiré) et un **nouvel objet directionnel** de la seconde couleur,
à densité variable et sans sous-couche, est inséré **immédiatement après le fond**,
pas en fin du motif. Les fils de la proposition sont conservés si leurs couleurs
n'ont pas été modifiées ; les couleurs manuelles restent libres. Une seule
annulation défait l'ensemble. **Orientation des lignes** : l'écart entre lignes ne
peut varier que perpendiculairement aux lignes ; le dessus hérite pourtant du
guide du tatami (angle d'origine). Si les lignes courent le long de l'axe de
couleur, le dégradé ne peut espacer presque rien et le fondu est écrasé (défaut
trouvé 2026-10 : sur une rampe horizontale avec un guide horizontal, il restait
plus de 80 % des lignes d'un remplissage uniforme). L'action remplace donc les
guides du dessus par un guide droit perpendiculaire à l'axe
(`stitch_generation::align_guides_across_density_gradient`, test
`blend: rows are turned across the colour axis…`). Le même alignement est
appliqué **à la génération** (`build_sectors`) pour tout remplissage portant un
`density_gradient` utile : des guides courbes conservés ou ajoutés après coup
(cas `TheyAskedForIT.osp`, 14 guides longeant l'axe vertical, rangées non
espacées et « fils parasites » sombres sur le fond) sont ignorés au profit du
guide perpendiculaire à l'axe (test `directional density gradient ignores guides
running along its axis`). Conséquence : un remplissage à dégradé ne suit plus
ses guides manuels. Les liaisons cousues entre
lignes (`sewMax`, 4 × l'écart) suivent l'écart **local** du dégradé et non son
maximum : avec 4 mm → 0,4 mm, une liaison de 16 mm traversait la zone dense
(`TheyAskedForIT.osp` : 50 → 11 avertissements « point long », au prix de plus
de sauts, donc de coupes, invisibles une fois le fil coupé).
Cause racine des petits sauts parasites : une liaison entre deux fins de rangées
voisines était refusée dès que son segment sortait de la région, or un contour
en escalier (vectorisation pixel) ou une fente de moins de 1,5 mm le fait sortir
d'un cheveu ; elle devenait un saut. Une liaison de 1,5 mm au plus est désormais
cousue (`linkInside`). `TheyAskedForIT.osp` : 811 → 360 sauts (fond 410 → 57,
dessus 150 → 52).

**Zone de transition** : par défaut la rampe d'écart couvre toute l'étendue de
la forme le long de l'axe. Les champs **Début / Fin de la transition** (en % de
l'axe, 0 → 100 %, au moins 5 % d'étendue) la restreignent à une portion :
`stitch_generation::narrow_gradient_zone` rapproche `from` et `to` le long de
l'axe, et le générateur bornant la position sur l'axe (`SpacingField::at`), l'écart
reste égal à `spacing_from` avant la zone et à `spacing_to` après : deux **aplats
nets** aux extrémités, sans champ de plus dans le document ni dans le `.osp`. La
zone s'applique dans `blendLayersFor`, donc à l'aperçu comme à la création, y
compris à la proposition issue de l'image. Valeurs par défaut (0 % et 100 %) :
sortie identique à l'ancienne. Dans l'inspecteur, modifier l'écart de fin d'un
dégradé existant **conserve son axe** (donc sa zone) tant que l'angle affiché ne
change pas ; changer l'angle recalcule l'axe sur toute la forme.

**Courbe de transition** : par défaut l'écart varie linéairement entre `spacing_from`
et `spacing_to`. Le menu **Courbe de transition** du dialogue choisit un profil
(`document::DensityCurve`, champ `DensityGradient::curve`) qui remappe la position
`t` sur l'axe avant l'interpolation (`SpacingField::shaped`) : **Linéaire** (`t`,
comportement historique), **Douce** (`3t² − 2t³` : départ et arrivée adoucis) et
**Marquée** (`6t⁵ − 15t⁴ + 10t³` : aplats plus longs, changement plus rapide au
milieu). Les trois valent 0, 0,5 et 1 en t = 0, 0,5 et 1 et sont monotones : les
écarts restent entre `spacing_from` et `spacing_to`, donc la grille et la fenêtre
d'auto-proximité du générateur sont inchangées. En profil complémentaire
(`interpolate_density`), le fond reçoit la même courbe avec les écarts échangés :
la somme des densités des deux couches reste constante quelle que soit la courbe
(`make_color_blend_layers`). La clé `.osp` n'est écrite que hors linéaire : un
fichier sans courbe est identique octet pour octet. Dans l'inspecteur, la courbe
d'un dégradé existant est conservée quand on modifie l'écart de fin ou l'angle (il
n'y expose pas encore de réglage de courbe) ; `set_params` et `inspect` la
nomment `curve` (`linear | smooth | steep`).

Déterministe : aucune graine, itération dans l'ordre d'entrée. Limites : un seul
plan (un dégradé par région, trois courbes prédéfinies, pas de points de contrôle),
deux couleurs maximum par région.

### Fondre deux zones voisines

**Forme ▸ Fondre deux zones** (Ctrl+Maj+F), avec exactement deux formes
sélectionnées, chacune avec un remplissage (tatami ou directionnel) : les deux
formes sont **unies** (comme *Unir* : la première sélectionnée, A, survit et
garde son remplissage comme **fond**), et un remplissage directionnel de la
couleur de B est ajouté **dessus**, sur toute la forme unie. Son dégradé de
densité (`stitch_generation::density_gradient_between`) va du centre de A vers
celui de B sur l'étendue des deux zones : écart large (4 mm) côté A, serré
(0,4 mm) côté B ; les lignes sont perpendiculaires à cet axe. Contrairement au
fondu à deux fils, le même dialogue propose initialement **les couleurs des deux
zones**, sans analyse de l'image. Les couleurs, l'angle et les écarts sont
modifiables avant l'union ; Annuler préserve les deux zones. Les couleurs doivent
être distinctes. Chaque forme doit avoir un seul remplissage compatible ; une
forme non remplie ou un couple de couches déjà créé désactive l'action.
Une seule annulation défait l'ensemble. La zone de transition (début / fin en %
de l'axe) et la courbe de transition se règlent dans le même dialogue.

### Fondu à la frontière entre deux zones (Studio, 2026-11-17)

Contrairement à « Fondre deux zones », qui unit les formes et pose un dégradé droit, le
**fondu à la frontière** garde les deux zones et fait entremêler leurs fils sur une bande de part
et d'autre de leur limite commune, quelle que soit sa forme (courbe, en S, en plusieurs
morceaux). Spécification complète : `docs/studio-fondu-frontiere-spec.md`.

- **Créer** : sélectionner deux zones remplies voisines (tatami ou directionnel sans dégradé),
  puis dans l'inspecteur régler la bande dans chaque zone (0,5 à 15 mm) et la transition (douce,
  moyenne, nette) et cliquer « Créer le fondu ». Même chose depuis la palette (Ctrl+K) :
  « Fondu à la frontière… ».
- **Régler / retirer** : sélectionner une des deux zones ; le panneau résume le fondu et
  applique chaque réglage immédiatement (un pas d'annulation) ; « Retirer le fondu » revient à
  deux aplats nets.
- **Depuis l'image** : « Proposer les fondus à la frontière depuis l'image » cherche les limites
  douces entre zones voisines et pose les fondus correspondants en une étape.
- Même angle et même écart des deux côtés (deux tatamis) : chaque rangée de la bande est cousue
  par un seul fil. Sinon les deux fils se superposent dans la bande.
- CLI / MCP : opérations `blend_boundary` (dont `frontier` pour une frontière tracée),
  `unblend_boundary`, `propose_boundary_blends`.
- Non validé sur tissu ; pas encore de tracé ni de poignées sur le canevas.

### Éditer les couches et preuve des correctifs UI (2026-10-13)

Dans **Propriétés**, **Couches de couture · ordre de couture** sélectionne
explicitement le fond ou le dessus d'une même forme. **Couleur de cette couche…**
modifie uniquement la couche choisie, en retirant son association à un fil réel
si nécessaire ; undo restaure couleur et fil. Le panneau Fils respecte aussi
cette sélection explicite. Convertir une couche en tatami ou en contour ne
supprime pas l'autre couche.

Deux causes racines ont été reproduites :

- L'inspecteur affichait `row_spacing` (0,4 mm) au lieu du réel
  `density_gradient->spacing_from` (3,2 mm dans la repro). Modifier l'angle
  écrasait alors l'écart initial. Affichage et recalcul utilisent désormais
  la valeur du gradient, sans modifier l'écart initial lors d'une rotation.
- `ConvertFillGroupCommand` supprimait **tous** les objets de même
  `source_vector`, confondant les sections d'un ancien réseau satin avec les
  couches indépendantes d'un fondu. Elle ne retire désormais que les sections
  satin à rails lorsque l'ancre était elle-même de ce type. Le test de
  non-régression du réseau satin multi-sections reste applicable.

Preuves : les six reproductions initiales de `test_color_blend` étaient rouges ;
la suite vérifie aussi création manuelle, proposition raster, validation,
inversion, ordre, choix de couche, couleur, conversion, undo/redo et relecture
`.osp`. `test_undo_stack.cpp` vérifie les conversions des deux couches et le
cas mixte fond + sections satin. Aucun champ persistant ni algorithme de
génération n'a été ajouté pour cette intégration UI.

## Remplissage en dégradé (Uni / Dégradé)

Pour l'utilisateur, un dégradé est **un seul remplissage** d'une forme, choisi
dans **Propriétés ▸ Remplissage : Uni / Dégradé** ; pour la machine, ce sont les
deux couches du fondu décrit plus haut (fond + dessus). Aucun second moteur :
tout passe par `make_color_blend_layers`.

**Représentation persistante : aucune donnée nouvelle.** Le couple est déjà
défini par `ColorBlendLink` (liens réciproques, rôle fond/dessus, même
`source_vector`, `.osp` inchangé). La description logique
(`project_ops::GradientFillSpec` : deux couleurs et fils, `DensityGradient` de
la couche de dessus — axe, écarts, courbe —, profil complémentaire ou fond
uniforme) est **relue** des deux couches par `describe_gradient_fill` ; les
anciens couples, créés par le dialogue avant cette version, sont donc
rééditables tels quels. Une métadonnée enregistrée à part aurait pu diverger
des couches ; relire garantit la cohérence. Un lien invalide ou orphelin ne se
résout pas (`find_color_blend`) : le remplissage redevient ordinaire, aucun
autre objet n'est touché.

La zone de transition n'est pas stockée non plus : c'est la position de `from`
et `to` le long de l'axe, en fraction de l'étendue de la forme mesurée sur cet
axe (`gradient_zone` / `gradient_with_zone`). Elle suit donc déplacements,
rotations et mises à l'échelle (le gradient est transformé avec la forme).

Service Qt-free `libs/project_ops/gradient_fill.hpp` (aucune logique de dégradé
dans les widgets) :

| Fonction | Rôle |
|---|---|
| `describe_gradient_fill` | relit le dégradé depuis l'une ou l'autre couche |
| `default_gradient_fill` | valeurs par défaut : 4 → 0,4 mm, axe horizontal, deux couleurs distinctes, profil complémentaire |
| `gradient_incompatibility` | motif affiché dans l'inspecteur (satin/contour, plusieurs remplissages sur la forme…) |
| `make_create_gradient_fill_command` | Uni → Dégradé en une commande (fond, dessus inséré juste après, lien, ordre) |
| `make_edit_gradient_fill_command` | réédition globale : mêmes ids, même rang de couture, **aucune** couche créée |
| `make_remove_gradient_fill_command` | retour à l'uni (couleur du fond), une annulation |
| `drag_gradient_handle` | calcul pur des poignées Début, Fin, Axe entier, Orientation |

Les points d'entrée existants convergent : le dialogue « Fondu de couleurs à deux
fils… » et « Fondre deux zones » créent via `make_create_gradient_fill_command`
(avec, pour deux zones, l'union placée avant dans la même commande composite) ;
le dialogue en mode édition (`setEditing`, bouton *Assistant* de l'inspecteur,
`MainWindow::reeditColorBlend`) rebâtit le **même** couple. L'aperçu des points
du dialogue et la validation utilisent la même construction
(`blendGradientFor` + `make_color_blend_layers`) ; un test vérifie que la
commande applique exactement les paramètres construits par le moteur.

**Cohérence du couple** : sélectionner l'une des couches affiche le dégradé
entier ; supprimer une couche supprime le couple (`removeEmbroideryCommand`, une
annulation) ; copier/coller et dupliquer remappent les liens et laissent
l'original intact ; déplacer, tourner, mettre à l'échelle transforment `from`/`to`
(déjà le cas). **Limite** : une mise à l'échelle non uniforme déforme l'axe et le
guide perpendiculaire séparément ; ouvrir l'assistant ou toucher une poignée
recalcule le guide.

**Poignées du canevas** (`Broderie ▸ Poignées du dégradé`, bouton de l'inspecteur,
barre contextuelle) : contexte d'édition explicite, un mode à la fois avec les
autres modes d'édition. *Début* et *Fin* glissent le long de l'axe (Maj : les deux
bords bougent symétriquement, ce qui règle l'étendue), *Axe entier* translate
l'axe en gardant l'étendue et sans sortir de la forme, *Orientation* tourne l'axe
autour de son centre (les rangées restent perpendiculaires à l'axe : l'axe du
dégradé n'est pas l'angle des rangées d'un tatami). Pendant le glisser seule
la ligne d'axe est redessinée ; au relâchement UNE commande est exécutée (Échap
annule le geste) et les points sont régénérés par le chemin habituel
(`effective_sequence`). L'aperçu des points n'est **pas** une simulation
fidèle du tissu.

Tests : `tests/unit/project_ops/test_gradient_fill.cpp` (cœur : aller-retour,
réédition sans nouvelle couche, zone, poignées, déplacement, collage, `.osp`,
lien orphelin, déterminisme) et `tests/unit/desktop/test_gradient_fill.cpp` (bascule
Uni ↔ Dégradé avec undo/redo, réédition en un pas, inversion, zone invalide,
glissés réels souris sur les poignées, suppression du couple, capture des deux
thèmes).

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
- **Générer un guide depuis la forme** (`directional_guides_from_region`,
  axe médian du pipeline auto-satin, sans aucun satin) : le chemin le plus long
  du squelette donne le guide principal ; chaque branche restante d'au moins
  `min_path_length` (1,5 mm) reçoit son propre guide, ancré sur le squelette
  déjà couvert (composante déconnectée : sur sa plus longue arête), dans la
  limite de `max_guides` (8). Glouton, ordonné par (longueur décroissante, id) :
  déterministe. Avant (2026-10), un seul guide était produit : les lobes d'une
  forme ramifiée n'avaient aucun guide proche et restaient mal couverts. L'action
  **remplace** les guides existants — y compris le guide droit posé par la
  conversion depuis un tatami — au lieu de s'y ajouter ; les lignes de rupture
  sont conservées. Option `simplify_tolerance` (op : `simplify_mm`, [0 ; 2] mm,
  0 par défaut = historique) : Douglas-Peucker sur les points rééchantillonnés
  avant le lissage, pour des guides à peu de nœuds retouchables à la main.
- Toute édition passe par `EditDirectionalFillCommand` (ou
  `SetStitchParamsCommand` depuis l'inspecteur) : undo/redo exact.

### Gestion des guides dans Studio (2026-11-28)

Jusqu'ici, l'outil Guide de l'espace Points de Studio savait seulement **tracer**
un guide et le panneau Satin **tout effacer**, derrière « Saisie par
coordonnées » : les guides tracés n'étaient ni visibles ni retouchables. Les
outils **Guide** (D) et **Rupture** (Maj+D), sur un tatami ou un remplissage
directionnel, affichent désormais au canevas les guides (ou ruptures) existants
avec une poignée par nœud :

- **glisser un nœud** remodèle le guide (ses tangentes, relatives, le suivent) ;
- **Maj+clic sur un tracé** retire ce guide seul (désignation à 9 px du tracé
  aplati, le plus proche l'emporte) ;
- seuls les nœuds du guide **survolé** sont dessinés et saisissables, les autres
  guides restent en trait fin (sur un motif réel, huit guides auto formaient sinon
  un amas de poignées) ;
- la **barre d'options** affiche le nombre de guides et propose « Guides auto »
  (opération `directional_guides` avec `simplify_mm` 1 : une poignée par
  inflexion au lieu d'un nœud tous les 2,5 mm), « Retirer le dernier » et
  « Tout retirer » ; un tracé à main levée est simplifié à 0,25 mm ;
- le panneau Satin montre la section « Guides de direction » (nombre, Guides
  auto, Retirer) pour tout remplissage guidable, sans ouvrir la saisie par
  coordonnées.

Chaque action est UNE étape d'annulation. Services sans Qt dans
`apps/desktop_v2/core/direction_guides.hpp` (`fillGuideCurves`, `fillGuideAt`,
`moveFillGuideNode`, `removeFillGuide`, `autoDirectionGuides`,
`canDirectionGuide`) ; adaptateur `SatinController`, gestes `DigitizeGestures`.
Le satin automatique garde son propre mode (ligne centrale du satin guidé).

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
- Champ lissé : régresse légèrement sur les formes effilées (croissant, corne) ; non testé sur
  des contours d'image réels. Le plus long chemin des guides automatiques retombe sur une seule
  arête au-delà de 64 arêtes de squelette.
- Phase 4 (fondu de couleurs) : disponible via **Broderie ▸ Fondu de couleurs à
  deux fils** (voir la section du même nom). Limites : un dégradé linéaire par
  région ; les deux objets partagent leur forme source, mais leurs paramètres
  ne sont pas liés (modifier l'un ne met pas l'autre à jour). L'ordre initial
  fond puis dessus est garanti à la création ; il peut ensuite être modifié
  volontairement dans le panneau Ordre. Pas de profils non linéaires ni de
  validation sur machine réelle.

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
