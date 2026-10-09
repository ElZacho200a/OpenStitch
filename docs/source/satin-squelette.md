# Satin par squelette et traversées orientées

Public : développeur, mainteneur. État : **moteur de référence de l'auto-satin**,
**non validé sur machine**. Ce chapitre décrit l'implémentation réelle
(`libs/auto_satin/src/skeleton_satin.cpp` et fichiers voisins) ; la spécification
de conception et le registre complet des problèmes sont dans
`specs/plans/satin-squelette-traversees.md`.

## Pourquoi l'ancien auto-satin a été remplacé

L'ancien moteur construisait **deux rails** le long du squelette, les appariait
(`ladder_correspondence`), posait des barreaux, réparait les jonctions, puis
validait la couverture. Il a été retiré de l'auto-numérisation (commit `18de427`)
parce que ses résultats polluaient le dessin : éligibilité trop permissive
(`2A/P ≤ largeur max`), couverture partielle acceptée, mosaïque de sections satin
et de pièces de tatami de repli.

Limites concrètes de cette architecture, toutes documentées dans `satin.md`
(archive) : rails inversés ou croisés, barreaux non triés, ajustement paramétrique,
décomposition récursive coûteuse (SGSD, 16 à 44 s sur une image réelle), formes
branchées jamais complètes (`multi_neck`, `star5`, `comb`, `E`).

Un fait structurant : les « rails » étaient déjà les extrémités de cordes
perpendiculaires au squelette. Le nouveau moteur garde cette primitive de mesure
mais supprime la construction de rails, l'appariement et la décomposition. Les
problèmes de **géométrie** (jonctions, coudes, pointes, bruit du squelette) ne
disparaissent pas avec elle : ils sont traités explicitement (ci-dessous).

## Principe

Pour une région Ω et son squelette (Zhang-Suen, `SkeletonGraph` élagué) :

1. Chaque chaîne d'arêtes du squelette (les nœuds de degré 2 ne sont pas des
   coupures) donne un **axe** lissé, paramétré par sa longueur d'arc.
2. On échantillonne l'axe. À chaque échantillon `P(s)`, une **orientation** `g(s)`
   définit la droite `P + t·(cos g, sin g)`.
3. La droite est intersectée avec la région ; la **corde** est l'intervalle
   intérieur qui contient `P`. Ses extrémités `A` et `B` sont les points de la
   traversée, sans rail ni appariement.
4. Les traversées d'une colonne passent par les finitions communes du satin
   (`finish_satin_stations` : terminaisons, points courts, sous-couches,
   compensation, fractionnement, sauts, verrous).

Trois grandeurs distinctes, jamais confondues : l'abscisse `s` sur l'axe,
l'espacement `ρ` entre traversées, la longueur `L` d'un point de broderie.

## Orientation `g(s)`

- **Défaut, par branche** : `g = α(s) + π/2` (perpendiculaire à la tangente).
  La spécification initiale prévoyait un angle absolu de 0° pour les squelettes
  « complexes » ; cette règle a été **écartée** : sur une branche parallèle à `g`,
  l'espacement perpendiculaire `h·|sin(g − α)|` tend vers 0 et la couverture
  latérale est nulle (échec sur les traits horizontaux de E, T, H, L). La
  distinction simple/complexe disparaît : chaque branche reçoit sa perpendiculaire.
- **Plancher** : `|sin(g − α)| ≥ 0,17` (≈ 10°). Une orientation plus proche de la
  tangente est ramenée au plancher et comptée (`clamped_angle`).
- **Guides** : un guide est un **point du repère du modèle** et un angle modulo π,
  relatif (écart à la perpendiculaire) ou absolu. Il est projeté sur l'axe à la
  génération ; aucun identifiant de branche n'est persisté (ils changent à chaque
  édition du contour). Entre deux guides l'angle est interpolé linéairement sur
  l'arc le plus court modulo π ; un écart d'exactement 90° tourne dans le sens
  positif (règle déterministe). Avant le premier guide et après le dernier : la
  valeur du guide le plus proche. Deux guides absolus égaux donnent un angle
  absolu constant. Un guide à plus de `max(2 r, 1,5 mm)` de l'axe est **orphelin** :
  signalé, ignoré.

Un trait tracé sur le canevas fixe un guide **absolu** (ancre = début du trait,
angle = direction du trait).

## Cordes et cellules

- La droite est intersectée avec tous les polygones (extérieur et trous, règle
  pair-impair) ; règle **demi-ouverte** aux sommets pour que la parité reste
  correcte quand la droite passe exactement par un sommet.
- **Cellules** : la région est partagée en cellules, une par morceau de colonne
  (une chaîne coupée aux coudes), selon le site le plus proche. Chaque corde est
  **écrêtée à sa cellule**, avec un recouvrement `ε` (0,2 mm) qui supprime les
  interstices. Aux jonctions, cela revient à des lignes de division
  bissectrices (idée d'EP0761860B1, rev. 3 à 5) et, aux coudes, à une coupe en
  onglet. Sans cet écrêtage, une corde au droit d'un nœud ou d'un coude traverse
  la branche voisine (jusqu'à 36 mm mesurés sur le tronc en « ] »).
- **Coudes** : changement de cap mesuré sur une fenêtre `±W` au-dessus de 35° ;
  on coupe au **centre du plateau** de la mesure (couper à son premier point
  démarrait le morceau suivant dans la partie droite du bras, avec une tangente
  fausse).
- **Garde de rayon** : une corde plus longue que `2,5 r + 60 µm` (r = rayon
  inscrit sur l'axe) est écrêtée à cette borne et comptée (`radius_guard_hits`).
- **Bouts libres** : le squelette de Zhang-Suen s'arrête environ un rayon avant le
  bord. L'axe est prolongé en ligne droite tant que le point reste intérieur
  (marge de 1 µm), au plus `3 r + 1 mm`. Aucun refus des cordes étroites aux
  pointes ; longueur minimale de fil 0,3 mm.
- **Anneau** (squelette sans nœud) : axe **fermé**, périodique, point de départ
  canonique (plus petit `x`, puis `y`). La dernière traversée répète la première
  pour fermer la couture. Les cycles et boucles perdus par `build_skeleton_graph`
  (qui exclut le retour au pixel d'origine) sont récupérés depuis le squelette brut
  uniquement s'ils sont fermés ou si leurs **deux** bouts touchent le graphe ; une
  épine à bout libre est une branche élaguée à juste titre et ne revient pas.

## Densité adaptative

Pour une traversée de demi-longueurs `a` (côté −u) et `b` (côté +u), avec
`σ = sin(α − g)` et `g' = dg/ds`, l'espacement perpendiculaire à distance `t` de
l'axe vaut `h·|σ + t·g'|` (dérivé). Le pas sur l'axe est

```
h(s) = ρ / max( |σ − a·g'| , |σ + b·g'| ),   h ∈ [50 µm, 3ρ]
```

c'est-à-dire que l'espacement au bord le **plus écarté** vaut `ρ` : aucun trou
du côté extérieur d'un virage. Le côté intérieur, plus dense, est traité par les
points courts existants. Les traversées voisines ne se croisent pas tant que
`|g'|·d_côté < |sin(g − α)|` (pour une perpendiculaire : `κ·d_intérieur < 1`).
Sur un demi-anneau de rayons 3 à 7 mm, l'espacement du bord extérieur est `ρ` à
8 % près (test).

## Fractionnement `Lmax` / `y`

`SatinConfig::max_stitch_length` est le seuil `Lmax` ; `split_length` est la
longueur de segment `y` (0 = `Lmax`, comportement historique). Si `L > Lmax`, la
traversée est découpée en `⌈L / y⌉` segments égaux, avec décalage déterministe
des pénétrations intermédiaires (décalé, ou jitter à graine fixe).
`split_connecting_throws` fractionne **aussi** le trajet retour `B_i → A_{i+1}` du
zigzag : le satin à rails ne fractionnait que les traversées `A → B`, laissant la
moitié des points de fil à pleine largeur. Exemple testé : 20 mm, `Lmax` 7 mm,
`y` 4 mm → points de 4 mm maximum.

## Structures de données

`document::AutoSatinParams` (5ᵉ alternative de `StitchParams`, ajoutée en fin :
les index historiques sont inchangés) :

| Champ | Rôle |
|---|---|
| `guides` | `AutoSatinGuide { anchor (µm), angle, absolute }` |
| `spacing` | `ρ`, espacement cible au bord le plus écarté |
| `split_stitch`, `split_threshold`, `split_length` | fractionnement (`Lmax`, `y`) |
| `short_stitch`, `cap_start/end`, `lock_*`, `pull_*`, `push_*` | finitions communes |
| `center_underlay`, `underlay_edge`, `underlay_zigzag` | sous-couches |
| `entry_point`, `exit_point` | sens de couture |

L'objet **suit la région de son vecteur source** : les traversées sont
recalculées à la demande (ADR-014), jamais stockées. Translation et mise à
l'échelle du vecteur déplacent aussi les ancres de guides et les points
d'entrée/sortie, avec annulation exacte. Contrairement au satin à rails, il ne
porte aucune géométrie propre.

## Format `.osp`

Type JSON `autoSatin`. Toutes les clés sauf `type` sont optionnelles (défauts du
modèle). `kSchemaVersion` passe de 4 à 5 : un ancien exécutable refuse proprement
un fichier plus récent au lieu d'échouer sur un type de point inconnu. Les anciens
projets se chargent inchangés. **Les objets satin à deux rails (`satin`) se
chargent et génèrent leurs points comme avant** (ils restent le satin manuel).
Il n'existe pas de migration automatique d'un satin à rails vers un auto-satin :
les rails édités à la main ne sont pas déduits du squelette.

## Diagnostics (non bloquants)

`SkeletonSatinResult::diagnostics` : échantillons hors région, traversées trop
courtes, orientations ramenées au plancher, gardes de rayon, guides orphelins,
morceaux, messages de refus nommés (région compacte, squelette vide), et, sur
demande (`measure_coverage`), couverture estimée, recouvrement moyen et aire non
couverte. L'estimation rasterise les triangles balayés par deux traversées
consécutives sur un masque de 100 µm érodé d'un pixel ; elle concorde avec une
mesure indépendante par grille à 2 points près. `skeleton_satin_to_svg` et la
commande `satin-auto-debug` du CLI produisent un SVG (contour, axes, traversées,
zigzag). Aucun seuil de qualité textile n'est imposé avant essai machine.

## Résultats sur le corpus de formes

Mesures du 2026-10 (couverture / recouvrement, `make_shape`), planchers gelés dans
`tests/unit/auto_satin/test_skeleton_satin.cpp` :

| Forme | Couverture | Recouvrement | Ancien moteur (source : `satin.md`) |
|---|---|---|---|
| rectangle | 1,000 | 1,00 | plancher 0,96 à 1,00 |
| capsule, ruban, S | 0,997 à 0,999 | 1,00 à 1,07 | n/d |
| Y, T, croix, H | 0,996 à 1,000 | 1,00 à 1,06 | plancher 0,858 à 0,887 |
| E, tronc en ] | 0,993 | 1,00 à 1,01 | refus (coude à 90°) |
| trident | 0,991 | 1,03 | refus à 0,05 mm de pixel |
| star5, comb, multi_neck, dumbbell | 0,979 à 0,997 | 0,98 à 1,03 | non « Complete » avant correctifs |
| deux trous | 0,994 | 1,06 | n/d |
| anneau, anneau + branche | 0,9998 | 1,00 | 0 région en SGSD |

## Limites restantes

- **Aucune validation sur machine.** La couverture géométrique n'est pas la
  qualité textile.
- `deep_channel` (U dont les bras font 16 mm de large) : les bras sont des blocs,
  pas des rubans, et l'élagage retire leurs branches ; la couverture reste
  autour de 65 %. Le moteur le **rapporte** (diagnostic) mais ne le résout pas.
- Disques, pétales et formes compactes : refus explicite (« région compacte »).
  Le satin tournant par anneaux de l'ancien code a disparu avec lui.
- Anneau avec plusieurs trous et cycles sans nœud : cas non couverts.
- Le squelette reste celui de Zhang-Suen (bruit de pixel, tangentes lissées sur
  600 µm). Un axe médian calculé sur le polygone (diagramme de Voronoï de
  segments) supprimerait cette source de bruit ; il n'est pas implémenté. La
  comparaison chiffrée est à faire avant d'ajouter une dépendance (Boost.Polygon,
  BSL-1.0).
- Le squelette est recalculé à chaque génération (aucun cache entre appels).
- Le fil peut se croiser du côté intérieur d'un virage très serré
  (`κ·d_intérieur ≥ 1`) : les points courts atténuent, ne garantissent pas.

## Fichiers

| Fichier | Contenu |
|---|---|
| `libs/auto_satin/src/chord.*` | cordes multi-intervalles, règle demi-ouverte |
| `libs/auto_satin/src/orientation.hpp` | `g(s)`, modulo π, clés relatives et absolues |
| `libs/auto_satin/src/axis.*` | axe lissé, ouvert ou fermé |
| `libs/auto_satin/src/axis_sampler.*` | pas adaptatif, bouts, garde de rayon |
| `libs/auto_satin/src/skeleton_satin.cpp` | chaînes, coudes, cellules, guides, anneaux |
| `libs/stitch_generation/src/generate.cpp` | `generate_auto_satin` |
| `apps/desktop/main_window_satin_auto.cpp` | création, guides, aperçu, squelette |

## Coudes serrés : croisements et éventail (2026-10)

Sur des bandes courbes à coude serré (rayon du coude inférieur à la largeur), trois défauts
ont été observés puis corrigés dans `libs/auto_satin` :

- **Nœud de fils côté intérieur** : les traversées d'un coude se coupent au centre
  instantané de rotation de l'orientation (`t* = −σ/g'`). Chaque traversée est bornée du
  côté convergent (`converge_keep`), puis une passe `trim_crossings` raccourcit, à chaque
  croisement résiduel, la traversée qui s'étend le plus loin de son point d'axe (l'autre
  reste entière). Les traversées quasi confondues (recouvrement de cellules) sont
  dédoublonnées, la plus courte étant retirée.
- **Secteur extérieur vide** : couper un coude en deux pièces perdait la rotation située
  juste à la coupe. `junction_fan` ajoute, côté extérieur uniquement, des cordes autour du
  point de coupe dont l'angle suit la rotation de la tangente (pas angulaire `ρ/portée`).
- **Saut après échec d'échantillon** : après une corde non émise, le pas retombe à `ρ/4`
  au lieu de `ρ`, pour ne pas enjamber un coude.

Garantie testée : sur tout le corpus, aucune paire de traversées d'une même colonne ne se
coupe (`crossing_pairs == 0`). Coût mesuré : `s` passe de 0,99 à 0,975 et `two_holes` de
0,985 à 0,980 de couverture estimée (planchers abaissés en conséquence). Non validé sur
machine.
