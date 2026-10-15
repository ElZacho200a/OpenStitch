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

## Formes compactes : axe synthétique (`compact_axis`)

Le squelette d'un disque se réduit à un point : sans axe, pas de traversée. Avec
`SkeletonSatinParameters::compact_axis` (`AutoSatinParams::compact_axis`, faux par
défaut, clé `.osp` optionnelle `compactAxis`), si aucune chaîne n'existe, le moteur
construit un **axe droit synthétique** :

1. centroïde de la région (extérieur moins trous) ;
2. direction = axe principal d'inertie (`geometry::principal_axis`) ; forme quasi
   isotrope (anisotropie <= 1,3) : horizontal par convention, déterministe ;
3. corde intérieure qui contient le centroïde, raccourcie de 5 % à chaque bout
   (bouts libres, prolongés par le sampler) ; le rayon en chaque point est la
   demi-corde perpendiculaire.

Les traversées sont perpendiculaires à l'axe : des cordes **parallèles** de
longueur variable (0 -> diamètre -> 0), comme un rond cousu à la main. Elles
valent au plus le diamètre : au-delà de `split_threshold` (7 mm) elles sont
fractionnées (décalé). Mesure : `disc_15mm`, `circle`, `petal` >= 90 % de
couverture (test). Le message « région compacte » est remplacé par un message
d'information. Sans l'option, le refus et ses tests sont inchangés.

**Limites** : pas de satin « en éventail » ou concentrique (le fil passe toujours
en ligne droite d'un bord à l'autre) ; un grand disque donne de longs points
fractionnés, d'où la borne `satin_compact_max_width` (12 mm) côté
auto-numérisation ; non validé sur machine.

## Limites restantes

- **Aucune validation sur machine.** La couverture géométrique n'est pas la
  qualité textile.
- `deep_channel` (U dont les bras font 16 mm de large) : les bras sont des blocs,
  pas des rubans, et l'élagage retire leurs branches ; la couverture reste
  autour de 65 %. Le moteur le **rapporte** (diagnostic) mais ne le résout pas.
- Disques, pétales et formes compactes : refus explicite (« région compacte »)
  par défaut. Option `compact_axis` (voir ci-dessous) pour les coudre en cordes
  parallèles. Le satin tournant par anneaux de l'ancien code a disparu.
- Anneau avec plusieurs trous et cycles sans nœud : cas non couverts.
- Le squelette reste celui de Zhang-Suen (bruit de pixel, tangentes lissées sur
  600 µm). Un axe médian calculé sur le polygone (diagramme de Voronoï de
  segments) supprimerait cette source de bruit ; il n'est pas implémenté. La
  comparaison chiffrée est à faire avant d'ajouter une dépendance (Boost.Polygon,
  BSL-1.0).
- Le squelette est recalculé à chaque génération (aucun cache entre appels).
- Le fil peut se croiser du côté intérieur d'un virage très serré
  (`κ·d_intérieur ≥ 1`) : les points courts atténuent, ne garantissent pas.

## Élagage à erreur bornée (2026-10-12)

Le squelette de Zhang-Suen d'un contour de pixels (ou d'un contour vectorisé)
porte des rameaux parasites. L'ancienne règle de `prune_graph` retire une branche
terminale si elle est courte en absolu (1,5 mm) *et* faible devant son rayon moyen ;
elle ignore combien la branche dépasse réellement de la forme.

`GraphCleanupParameters::max_protrusion` / `max_protrusion_to_radius` ajoutent un
critère à **erreur bornée**, dans l'esprit de l'élagage par résidu de frontière
(Ogniewicz) et des mesures de stabilité du squelette (λ-axe médian, *scale axis
transform*) : une branche terminale de longueur `L`, d'extrémité de rayon `r_E`,
issue d'une jonction de rayon `r_J`, ne dépasse du disque de la jonction que de
`h = L + r_E − r_J`. Tout disque `(c(s), r(s))` de la branche est contenu dans le
disque `(J, r_J + h)` parce que `s + r(s)` ne décroît pas (le rayon varie au plus
comme l'abscisse, propriété 1-lipschitzienne de la distance au bord). Retirer la
branche déplace donc la forme reconstruite de **au plus `h`** : on la retire si
`h <= max(max_protrusion, max_protrusion_to_radius · r_J)`. Une branche courte mais
saillante est conservée ; une branche longue mais noyée dans un gros disque est
retirée. Les candidates sont traitées par `h` croissant (égalité : indice
d'arête), jamais jusqu'à vider le graphe, et chaque retrait est diagnostiqué
(« branche terminale non significative »).

Valeurs par défaut du moteur (`stable_skeleton_analysis()`) : 0,4 mm et 15 % du rayon.
`AutoSatinParameters` seul, et donc le mode Contours, gardent 0 / 0 (inchangés).
Les clés de cache du squelette incluent ces champs. Mesures : corpus de formes
propres, mêmes colonnes et même couverture ; contours bruités (densifiés tous les
0,4 mm, bruit ±0,2 mm) : colonnes en moins (h 19 → 15, comb 23 → 21, cross
14 → 12, E inchangé) et couverture égale ou meilleure ; ±0,3 mm : E 53 → 43. Le bruit blanc
non corrélé de ces mesures est plus sévère que celui d'une photo : à confirmer sur
des contours réels. Ne traite pas les boucles parasites ni les ponts de bruit
(non terminaux).

## Façade des stratégies et comparaison (lot A, 2026-10-15)

`libs/auto_satin/include/openstitch/auto_satin/satin_strategy.hpp` introduit
`generate_satin(region, params)`, qui aiguille selon `SkeletonSatinParameters::strategy`
(`SatinStrategy` est défini dans `skeleton_satin.hpp`). `Skeleton` appelle
`generate_skeleton_satin` sans rien changer (test d'égalité octet pour octet sur neuf
formes) ; c'est le défaut. Toute génération de satin automatique doit passer par la façade,
pas par `generate_skeleton_satin`, pour respecter le choix de l'utilisateur. Une stratégie n'est ajoutée à
l'énumération que lorsqu'elle est réellement livrée ; aucun réglage n'est encore
persisté dans `AutoSatinParams` ni dans le `.osp` (lot C).

`SatinMetrics` (`measure_satin` / `measure_result`) fixe les mesures **géométriques**
communes aux stratégies, avant finitions : colonnes, traversées, paires de traversées
qui se coupent, longueur minimale / moyenne / maximale d'une traversée, plus long retour
`B_i -> A_{i+1}` du zigzag, couverture, recouvrement, pic de densité (`overlap_peak`,
triangles de fil sur un pixel de 100 µm), aire non couverte et **boîte englobante des
manques** (`uncovered_min` / `uncovered_max`). Elles ne disent rien des sauts, des coupes
ni du nombre de points exportés : ces mesures dépendent de `stitch_generation` et
viendront avec la comparaison sur séquence. Temps et mémoire ne sont pas dans la
bibliothèque (non déterministes).

Commande CLI :

```powershell
openstitch-cli satin-compare                           # tout le corpus, tableau markdown
openstitch-cli satin-compare --shape E --shape comb --format csv
openstitch-cli satin-compare --svg-dir out/satin --timing
```

Sans `--timing`, la sortie est déterministe : on peut la diffuser avant/après un
changement du moteur. Elle sert de **référence chiffrée** du moteur actuel avant d'y
comparer un axe médian vectoriel (lot B).

## Balayage parallèle (lot D, 2026-10-15)

`SatinStrategy::ParallelSweep` (`parallel_sweep.hpp`, `parallel_sweep.cpp`) ne passe pas par
le squelette. Des droites à angle constant, espacées de `spacing` (ajusté pour tomber juste
sur l'étendue de la forme, donc un peu inférieur), sont intersectées avec la région par la
même primitive de corde que le moteur de squelette (`line_intervals`, règle demi-ouverte,
trous inclus). Chaque intervalle intérieur est une traversée.

- **Bandes** : les intervalles de deux droites consécutives qui se recouvrent **un à un**
  prolongent la même colonne. Une division (un intervalle en recouvre deux) ou une fusion
  ferme les colonnes concernées et en ouvre de nouvelles : le fil ne traverse jamais le
  vide d'une concavité ou d'un trou (testé : tous les points intérieurs de toutes les
  traversées sont dans la région, sur dix formes dont `E`, `comb`, `star5`, `two_holes`).
- **Angle global** : par défaut perpendiculaire à l'axe principal d'inertie quand la forme
  est allongée (anisotropie > 1,3), horizontal sinon ; ou `sweep_angle_rad` si
  `sweep_auto_angle` est faux. Les **guides locaux ne sont pas appliqués** : ils sont
  comptés dans `orphan_guides` et signalés dans `messages`, jamais ignorés en silence.
- **Pas de cache** : coût linéaire, aucun raster (le raster de la couverture reste à la
  demande).
- Les colonnes d'un seul fil sont ignorées (pas de zigzag possible) et comptées.

Mesures du 2026-10-15 (`openstitch-cli satin-compare`, espacement 0,4 mm) : le balayage
couvre sans refus les formes compactes que le squelette refuse (`circle`, `petal` :
couverture 99,5 à 99,7 %), et la forme `deep_channel`, limite connue du squelette
(65 % → 99 %). Il est moins bon sur les formes branchées : une seule grande colonne
pour `cross`, `E`, `comb` (couverture 97,5 à 98,1 %, fils de 15 à 40 mm que le
fractionnement `Lmax` devra couper) ; `ring` donne 4 bandes à 96 %. Ce n'est pas un
« meilleur » moteur : c'est un autre compromis, choisi par l'utilisateur. Non validé sur
machine.

**Mesure de couverture corrigée** : les triangles de fil étaient échantillonnés aux coins
des pixels de 100 µm ; des traversées parallèles à un axe, espacées d'un multiple du pixel,
passaient exactement par les échantillons et étaient comptées deux fois (faux recouvrement
de 1,2 sur un rectangle) ou plus du tout. L'échantillon est désormais décalé d'un sous-pixel
fixe et irrégulier. Les planchers du corpus et leurs tests sont inchangés.

## Axe médian vectoriel (lot B, 2026-10-15)

`SatinStrategy::MedialAxis` (`medial_axis_satin.hpp`, `medial_axis.cpp`) change seulement
l'**axe** : le squelette de pixels de Zhang-Suen est remplacé par l'axe médian du polygone,
lu dans le diagramme de Voronoï de ses segments (Boost.Polygon, BSL-1.0, encapsulé). Tout
l'aval est commun (`finish_from_chains` dans `skeleton_satin.cpp`) : coupe aux coudes,
guides, cellules, éventails, croisements, couverture. Les guides s'appliquent donc comme
pour le squelette.

- **Axe** : on garde les arêtes primaires finies dont le milieu est dans la région (pas dans
  un trou), moins celles dont une extrémité est sur le contour (rayon inscrit < 0,75 µm) :
  ce sont les rayons vers les coins convexes. Les arcs paraboliques (sommet réflexe contre
  segment) sont échantillonnés à 8 µm de flèche (128 points au plus). Les sommets confondus
  au micromètre sont fusionnés.
- **Graphe** : jonctions, extrémités, continuations, comme le graphe du squelette raster ;
  mêmes règles de chaîne. Un cycle sans noeud (anneau) devient une **colonne fermée**.
- **Élagage** : le même élagage à erreur bornée que le squelette (`prune_graph` avec
  `analysis.cleanup`). L'axe médian est exact mais sensible au bruit du contour : chaque
  sommet convexe y porte un rameau.
- **Refus nommés, sans repli silencieux** : contour qui s'auto-intersecte (le diagramme de
  segments exige des segments qui ne se coupent qu'à leurs extrémités) ; plus de 6000
  segments.
- Pas de cache (comparable au squelette en temps : 1 à 480 ms sur le corpus).

Mesures du 2026-10-15 (`satin-compare`, 0,4 mm) : couverture égale à celle du squelette à
1 point près sur la plupart du corpus, **sans croisement**, et les formes compactes que le
squelette refuse sont couvertes (`circle`, `disc_15mm`, `petal` : 99,8 à 99,9 %).
`deep_channel` passe de 65 à 93 %. Moins bon sur `comb` (97,1 contre 99,5 %, 9 colonnes
contre 11) et `trident` (1 colonne contre 3, 98,3 contre 99 %). Non validé sur machine.

## Choix de la méthode par l'utilisateur (lot C) et comparaison (lot E)

Une région auto-satin porte sa méthode : `document::AutoSatinParams::strategy`
(`Skeleton`, `ParallelSweep`, `MedialAxis`) et, pour le balayage, `sweep_angle` facultatif.
**Le squelette reste le défaut**, y compris pour un `.osp` sans la nouvelle clé (voir
`project-format.md` pour la persistance par nom et la version v7 conditionnelle).

- **Une seule correspondance réglages → moteur** : `stitch_generation::auto_satin_engine`,
  utilisée par la génération de points, l'aperçu, l'inspecteur, l'API programmatique et
  l'export ; tous appellent `auto_satin::generate_satin` (jamais `generate_skeleton_satin`).
  Le préchauffage du cache de squelette est ignoré pour les autres méthodes (sans cache).
- **Inspecteur** : section « Méthode » (liste déroulante avec infobulle par méthode, angle des
  fils automatique ou fixe pour le balayage, avertissement que les guides locaux ne
  s'appliquent pas au balayage) et bouton **« Comparer les méthodes… »**. Changer de méthode
  est une modification annulable comme les autres réglages.
- **Création** (« Convertir en satin ») : si le squelette ne produit aucune colonne, les autres
  méthodes sont essayées **avant** le refus (disque, pétale, bloc large). La première qui
  fonctionne est *proposée* et nommée dans le dialogue ou la question ; rien n'est appliqué à
  l'insu de l'utilisateur. Le dialogue permet de changer de méthode (résumé recalculé) et
  de comparer. Sélection multiple : une méthode par forme, la préférée de l'utilisateur d'abord.
  Conversion par le type de points : même repli, nommé dans l'historique.
- **Comparer les méthodes** : tableau (colonnes, couverture, fil en double, aire non couverte,
  croisements, plus long fil, remarques) calculé **hors du fil d'interface** sur un instantané
  des contours, sans toucher au document ; fermer ne change rien ; « Utiliser cette méthode »
  ne crée **qu'une seule commande annulable**. **Aucun « meilleur automatique »** : le choix
  esthétique et textile revient à l'utilisateur ; une recommandation ne viendra qu'après
  mesures et essais sur machine.
- **API programmatique** : argument `strategy` (et `sweep_angle_deg`) de l'opération
  `auto_satin` ; champs `strategy` et `sweep_angle_deg` de `set_params` ; relus par `inspect`.
  Un nom inconnu est une erreur d'usage, document intact.
- **Clés de cache** : le résumé de l'inspecteur inclut désormais la méthode, l'angle et
  `compact_axis` (ce dernier en était absent).

Limites : l'auto-numérisation (`autodigitize::choose_satin`) et le lettrage continuent
d'utiliser le squelette ; non validé sur machine.

## Fichiers

| Fichier | Contenu |
|---|---|
| `libs/auto_satin/src/chord.*` | cordes multi-intervalles, règle demi-ouverte |
| `libs/auto_satin/src/orientation.hpp` | `g(s)`, modulo π, clés relatives et absolues |
| `libs/auto_satin/src/axis.*` | axe lissé, ouvert ou fermé |
| `libs/auto_satin/src/axis_sampler.*` | pas adaptatif, bouts, garde de rayon |
| `libs/auto_satin/src/skeleton_satin.cpp` | chaînes, coudes, cellules, guides, anneaux |
| `libs/stitch_generation/src/generate.cpp` | `generate_auto_satin` |
| `libs/auto_satin/src/satin_strategy.cpp` | façade des stratégies, mesures comparables |
| `libs/auto_satin/src/parallel_sweep.cpp` | balayage parallèle |
| `libs/auto_satin/src/medial_axis.cpp` | axe médian vectoriel (Voronoï de segments) |
| `libs/auto_satin/src/coverage.cpp` | mesure de couverture partagée par les stratégies |
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
