# Colonne satin

Public : utilisateur avancé, développeur.

> État : Présent dans le code : oui · Tests unitaires : oui · Tests visuels :
> partiels · Import/export DST : oui · Test sur machine réelle : **non** ·
> **Statut recommandé : expérimental** — génération satin simple à deux rails.
> Plusieurs propriétés d'un générateur satin correct manquent (voir
> *Limitations* ci-dessous). Vérifiez impérativement le résultat avant tout
> passage sur machine.

## Définition

Une colonne satin est un zigzag serré entre **deux rails** (bords gauche et
droit) qui remplit une bande. Elle donne un effet lisse et brillant, adapté aux
lettrages et aux bordures étroites.

## Modèle

`SatinParams` porte `rail_a` et `rail_b` (deux `geometry::Path`), la densité
(`density`), la compensation de tirage (`pull_compensation`), et l'activation
d'une sous-couche centrale (`center_underlay`).

## Génération

`fill_satin(rail_a, rail_b, config)` :

1. calcule une **correspondance locale monotone** entre les deux rails
   (`ladder_correspondence`, voir *Correction de l'appariement* ci-dessous) —
   les rails peuvent avoir des longueurs, courbures et échantillonnages
   différents ;
2. ré-échantillonne cette correspondance par pas fixe le long de sa **ligne
   médiane** (≈ perpendiculaire aux fils), et produit un **zigzag alterné**
   d'un bord à l'autre (L0, R0, L1, R1, …) ;
3. la **densité** fixe ce pas d'avancement, mesuré sur la ligne médiane (donc
   proche de la perpendiculaire aux fils, y compris en section inclinée ou
   courbe) ;
4. la **compensation de tirage** écarte les deux rails le long de leur
   médiatrice, pour compenser le resserrement du fil ;
5. la **sous-couche centrale** (optionnelle) est un point droit grossier sur
   l'axe, cousu avant le zigzag, dérivée de la même correspondance.

Le résultat (`SatinResult`) fournit les points de sous-couche, du satin, et la
**largeur maximale** rencontrée (pour l'avertissement).

## Paramètres

Valeurs par défaut lues dans `SatinParams`.

| Paramètre | Unité | Défaut | Effet |
|---|---|---|---|
| `density` | mm | 0,4 | pas d'avancement le long de la colonne (plus petit = plus dense) |
| `pull_compensation` | mm | 0 | élargit la colonne (compense la traction du fil) |
| `center_underlay` | bool | vrai | ajoute une sous-couche centrale (point droit sur l'axe) |
| `max_width` | mm | 9 | seuil d'avertissement de largeur excessive |

> Validation physique : non effectuée. La densité satin devrait idéalement se
> mesurer perpendiculairement au fil (voir l'avertissement ci-dessus).

## Correction de l'appariement rail gauche / rail droit (audit rails, 2026-08-01)

*État : Présent · Testé numériquement (fixtures géométriques dédiées) · non
validé simulateur/physique.*

**Défaut trouvé.** L'ancien appariement (`fill_satin` sans barreaux, et
l'interpolation à l'intérieur d'un intervalle entre deux barreaux dans
`fill_satin_columns`) associait les deux rails par la **même fraction
d'abscisse curviligne** appliquée indépendamment à chacun (ou, avec barreaux,
la même fraction `u` sur tout l'intervalle). Dès que les deux rails divergent
en longueur ou en courbure — virage, coude, largeur variable — cette
hypothèse de proportionnalité est fausse : les fils cessent d'être localement
perpendiculaires au ruban. Symptôme observé : orientation des points en
retard sur la direction locale des rails, éventails, quasi-croisements et
densité visuelle irrégulière dans un ruban courbe ou anguleux.

**Correction.** Remplacé par une correspondance locale **"ladder"**
(triangulation de bande par la diagonale la plus courte entre deux chaînes —
technique classique de géométrie algorithmique, indépendante de tout logiciel
de broderie) :

1. chaque rail est sous-échantillonné entre deux ancres exactes (barreaux, ou
   les deux extrémités de colonne si aucun barreau) à une résolution bornée
   (`maxStep`, fonction de la densité) — donne assez de détail même si les
   sommets d'origine sont épars (rail dessiné à la main, contour simplifié) ;
2. à chaque pas, la correspondance avance sur le rail dont le **prochain
   sommet forme la diagonale la plus courte** avec le point courant de
   l'autre rail (`ladder_correspondence`) — un garde-fou rejette une avance
   qui croiserait la diagonale précédente et force l'autre côté ;
3. cette correspondance dense est ré-échantillonnée par pas fixe le long de
   sa **ligne médiane** (`resample_by_medial_spacing`) : l'interpolation de
   (sa, sb) se fait dans le pas LOCAL du ladder qui encadre chaque cible,
   jamais sur tout l'intervalle — l'erreur reste bornée par la résolution du
   ladder, pas par la longueur de l'intervalle.

**Invariants garantis** (vérifiés par fixtures, voir ci-dessous) :

- **Monotonie** : les indices/abscisses avancent sur chaque rail dans le même
  sens que le parcours, jamais en arrière.
- **Absence de croisement** : deux fils consécutifs (et, mesuré sur les
  fixtures, deux fils quelconques) ne se croisent jamais.
- **Stabilité à l'échantillonnage** : la correspondance ne dépend pas du
  nombre de sommets d'origine de chaque rail (rails de résolutions très
  différentes acceptés).
- **Déterminisme** : aucune donnée non déterministe, résultat identique à
  entrée identique.
- **Complexité** : O(nA + nB) par intervalle entre deux barreaux (linéaire) —
  la ladder ne revisite jamais un sommet, comme la triangulation de bande
  classique dont elle s'inspire.

**Limite assumée** : à un coude **C0 franc** (angle vif sans congé), la
notion de « normale locale » est elle-même discontinue ; un seul fil au
sommet du coude absorbe nécessairement une déviation angulaire importante.
Aucun appariement ne peut faire autrement sans mitre/congé explicite (relève
de `ShortStitchMode`, hors périmètre de l'appariement de base). Ce que la
correction garantit dans ce cas : la déviation reste localisée à ce fil
unique (pas d'éventail étendu sur plusieurs fils) et aucun croisement
n'apparaît.

**Fixtures et métriques** (`tests/unit/stitch/test_satin_pairing_metrics.cpp`,
13 tests, indépendants des tests de non-régression `test_satin.cpp`) : ruban
droit (cas trivial), ruban en S, coude à 90° franc, largeur variable, cas
combiné courbe+coude+largeur (inspiré d'un ruban capturé par l'utilisateur),
plus la même correspondance via `fill_satin_columns` (barreaux), et 7 fixtures
adverses (revue corrective ci-dessous). Chaque test mesure croisements
(adjacents et toute paire), monotonie sur les deux rails, angle fil/normale
locale (moyenne + pire cas), continuité angulaire entre fils consécutifs,
régularité de la densité médiane, longueurs min/max, et déterminisme. Une
réplique isolée de l'ancien algorithme (jamais utilisée en production) sert
de référence comparative dans les mêmes tests. Résultats mesurés : ruban en
S, angle max fil/normale 2,7° (nouveau) contre 58,4° (ancien), angle moyen
1,2° contre 33,5° ; coude à 90°, 0 croisement (nouveau) contre 3 (ancien) ;
cas combiné, angle max 4,9° contre 44,3°, 0 croisement dans les deux cas sur
cette fixture précise mais éventail moyen réduit d'un facteur 12 (2,0° contre
24,0°).

### Revue corrective (audit adverse, 2026-08-01)

*État : Présent · Testé numériquement (fixtures adverses dédiées) · non
validé simulateur/physique.*

Audit ciblé de `ladder_correspondence` avec des fixtures délibérément
défavorables (rails tête-bêche, échantillonnage très asymétrique avec
doublons/segments nuls, longueurs très différentes + largeur quasi nulle,
épingle à cheveux ~170°, barreaux désordonnés/dupliqués). Deux défauts réels
trouvés et corrigés, un comportement clarifié :

- **Rails fournis tête-bêche** (bout 0 de A proche du bout N de B) : le
  commentaire de `fill_satin` exigeait déjà des rails « orientés dans le même
  sens », mais rien ne le vérifiait — un appel avec des rails inversés
  produisait un nœud papillon (chaque fil tend vers la diagonale opposée,
  croisements en O(n²)) au lieu d'un ruban. Aucun chemin de production
  actuel ne peut produire ce cas (`rails_from_contour` et
  `auto_satin::build_satin_columns` garantissent déjà le même sens), mais
  c'est une précondition silencieuse d'une fonction de bibliothèque publique
  — risquée pour un futur appelant (import, script, saisie manuelle). Corrigé
  par une détection bon marché (`opposite_orientation` : compare la somme des
  distances bout-à-bout dans les deux sens possibles) et une ré-orientation
  interne automatique, dans `fill_satin` et `fill_satin_columns`.
- **Barreaux non triés** : `fill_satin_columns` ne gardait un barreau que si
  sa projection avançait sur les deux rails par rapport au **dernier barreau
  GARDÉ dans l'ORDRE DU VECTEUR D'ENTRÉE** — un barreau placé en tête du
  vecteur mais loin le long de la colonne verrouillait cette référence et
  faisait rejeter silencieusement tous les barreaux suivants, repliant sur
  `fill_satin` sans barreaux (alors que la doc promet des barreaux toujours
  traversés exactement). Un barreau est une station transversale, pas un
  élément de séquence : il n'y a aucune raison que son ordre dans le vecteur
  soit signifiant. Corrigé par un tri par position projetée avant le filtre
  anti-croisement — le résultat ne dépend plus de l'ordre d'entrée des
  barreaux (vérifié : résultat bit-à-bit identique entre un vecteur trié et
  le même mélangé).
- **Barreaux dupliqués/quasi-dupliqués** : deux barreaux dont la projection
  n'avance que d'une quantité minuscule (mais non nulle) sur les deux rails
  créaient un intervalle dégénéré. Comme le garde-fou anti-croisement de
  `ladder_correspondence` n'agit qu'À L'INTÉRIEUR d'un intervalle (jamais
  entre deux intervalles voisins), les deux fils-ancres qui encadraient cet
  intervalle dégénéré n'étaient jamais comparés l'un à l'autre — s'il tombait
  près d'un virage serré, ils pouvaient se croiser (reproduit et corrigé,
  fixture "barreau dupliqué/quasi-dupliqué"). Corrigé en fusionnant (après
  tri) tout barreau dont la projection avance de moins de la moitié du pas de
  densité (`anchorMinGap = density / 2`) par rapport au dernier barreau
  gardé — élimine la cause (l'intervalle dégénéré) plutôt que de tenter de
  détecter le croisement a posteriori.
- **Échantillonnage très asymétrique / segments nuls / longueurs très
  différentes / largeur quasi nulle / épingle à cheveux serrée (~170°)** :
  déjà robustes sans modification — aucun croisement, monotonie et
  déterminisme préservés, aucune coordonnée dégénérée (vérifié par bornes
  larges plutôt que `isfinite` sur les micromètres entiers, qui ne peuvent
  pas représenter NaN).

**Complexité vérifiée** : le tri des barreaux ajouté est O(m log m) avec `m`
= nombre de barreaux (petit, jamais lié à la résolution des rails) — ne
change pas la complexité globale. Mesure empirique (release, ruban sinusoïdal
8 périodes, densité fixe) : temps de `fill_satin` stable (~6 ms) et nombre de
fils constant pour un nombre de sommets d'entrée par rail variant de 500 à
8000 — confirme l'absence de comportement quadratique cachée dans le nombre
de sommets d'origine.

**Fixtures ajoutées** (mêmes métriques que ci-dessus) : rails tête-bêche,
longueurs très différentes + largeur quasi nulle, épingle à cheveux (mesure
`crossings_total`, pas seulement `crossings_adjacent`, pour couvrir un
croisement non adjacent), barreaux désordonnés (comparaison bit-à-bit avec la
version triée), barreau dupliqué/quasi-dupliqué. S'ajoutent à la fixture
d'échantillonnage asymétrique avec doublons/segments nuls déjà présente.

## Colonne trop large

À la création, si la largeur dépasse le seuil recommandé, l'interface **prévient**
et propose de préférer un remplissage tatami — la limite physique n'est jamais
masquée.

## Rails automatiques

`rails_from_contour(contour)` découpe un contour fermé en deux rails, coupé aux
**deux sommets les plus éloignés** (les « bouts » de la colonne).

**Débordement — désactivé par défaut.** Cette heuristique ne connaît pas l'axe
de la forme : sur un contour **concave ou branchu**, les deux rails traversent
l'intérieur et les barreaux (rungs) enjambent les creux, ce qui fait **sortir
les points de la région**. Mesuré sur un projet réel, jusqu'à **57 % des
barreaux d'une colonne hors de sa région**. En conséquence :

- l'auto-numérisation **n'utilise plus** le satin naïf par défaut. Elle essaie
  d'abord `auto_satin::build_satin_columns` (`use_auto_satin`, défaut `true`) ;
  une forme refusée retombe sur le **tatami**. L'option
  `AutoOptions::use_naive_satin` (défaut `false`) ne subsiste que comme repli
  explicite pour essais/tests ;
- l'action **Broderie ▸ Convertir les satins auto en tatami** répare un projet
  qui contient déjà des satins débordants ;
- le satin reste créable **manuellement** (menu Broderie, ou clic droit ▸ *Type
  de points ▸ Colonne satin*) — à vérifier visuellement, c'est la même
  heuristique de rails.

Limitation : `rails_from_contour` reste une heuristique (utilisée seulement pour
la création **manuelle** de satin). La bonne méthode est le moteur géométrique
ci-dessous.

## Colonnes automatiques par squelette (`build_satin_columns`)

*État : Présent · Testé numériquement · Validé visuellement (SVG) · non validé
simulateur/physique.*

`auto_satin::build_satin_columns(region, params)` construit une ou plusieurs
**colonnes éditables** (`SatinColumnGeometry` : deux rails + **barreaux**) depuis
une région, via le pipeline : rasterisation → transformée de distance → squelette
(Zhang-Suen) → graphe élagué → satinabilité → **sections transversales** →
rails → barreaux → validation.

Pour chaque branche du squelette : l'axe est lissé (Chaikin) et ré-échantillonné
par longueur d'arc ; à chaque station on calcule la tangente puis la **normale**,
on l'intersecte avec le contour et on retient l'**intervalle intérieur encadrant
l'axe** (jamais la bounding box, jamais une association par index). Les deux
extrémités donnent les rails ; la stabilité gauche/droite vient du signe de la
normale (le rail A est toujours à gauche du sens de parcours). Les barreaux sont
posés aux extrémités, aux virages, aux changements de largeur et à intervalle
maximal. Un nettoyage anti-croisement retire les stations qui replieraient la
colonne.

### Extension des bouts ouverts jusqu'au bord réel

*État : Présent · Testé numériquement · Validé visuellement (SVG).* Activé par
défaut (`SatinColumnsParameters::extend_open_ends`), rétrocompatible.

**Défaut trouvé par revue** (mission « auto-satin béton », audit du squelette
existant confronté à la littérature — voir *Sources* ci-dessous) : un bout
**ouvert** du squelette (sans jonction) s'arrête, par construction de
l'amincissement (Zhang-Suen érode la forme progressivement depuis le bord),
sensiblement **avant** le bord réel de la région. Démontré visuellement sur
`capsule` (rectangle de 40 mm + deux demi-cercles de rayon 2,5 mm, longueur
bout-à-bout réelle 45 mm) : avant correction, la colonne s'arrêtait à ~38,8 mm
— les deux embouts arrondis (≈ 11 % de la longueur totale) restaient **entièrement
hors couture**, un rectangle purement rendu vide au bout de chaque colonne. Le
même retrait (proportionnel à la demi-largeur locale) affecte aussi un bout
**carré** : sur `rectangle` (bouts plats, 40 mm), le retrait mesuré est de
~2,55 mm par bout — un défaut générique de l'amincissement, pas spécifique aux
formes arrondies.

**Correction** (`extend_tip` dans `satin_column.cpp`) : pour chaque bout
**sans jonction** (un bout de jonction reste inchangé — il doit rester
exactement au nœud du squelette pour la reconciliation multi-sections), on
marche depuis la dernière station le long de la tangente sortante par pas de
`station_spacing`, en ré-évaluant une section transversale réelle (intersection
avec le contour, pas une approximation) à chaque pas tant qu'elle continue de
**rétrécir** (tolérance 5 % contre une marche qui déborderait dans une autre
partie de la forme). La fermeture finale est localisée par **bissection** sur
un test point-dans-région pur (robuste même quand la section transversale
devient numériquement dégénérée tout près du bord), avec une marge de sûreté de
quelques µm avant l'arrondi final en micromètres entiers — sans cette marge, la
bissection converge si près du bord analytique que l'arrondi peut faire
retomber le point de l'autre côté du polygone discrétisé (trouvé par un test
en échec, corrigé avant commit). Le point de fermeture reçoit une **largeur
plancher non nulle** (`tip_min_width`, 0,05 mm par défaut) plutôt qu'un barreau
littéralement nul, pour rester exploitable tel quel par `fill_satin_columns`.
Marche et bissection sont toutes deux **bornées** (200 pas / 24 bissections) :
jamais de boucle infinie, même sur une géométrie pathologique. Les rails
**se terminent désormais sur le contour réel** → plus de débordement structurel
ET plus de zone non couverte.

Vérifié : la longueur bout-à-bout d'une colonne `capsule` passe de ~38,8 mm à
45,0 mm (±1 mm) — la valeur géométrique exacte ; un bout carré (`rectangle`)
est également corrigé (40,0 mm au lieu de ~34,9 mm) ; les bouts de jonction
d'un réseau Y restent géométriquement identiques désactivé/activé (seuls les
bouts ouverts s'allongent) ; aucun barreau dégénéré après extension ; extension
déterministe ; le bascule `extend_open_ends=false` restaure l'ancien
comportement (SVG avant/après dans `tests/golden/auto-satin/columns-*.svg`).

**Sources.** Ce mécanisme reprend, en le réimplémentant intégralement à partir
des principes documentés (aucun code tiers consulté ni copié — projet
Apache-2.0, jamais de dépendance à du code GPL) : le brevet Pulse Microsystems
US6804573B2 *« Automatically generating embroidery designs from a scanned
image »* ([Google Patents](https://patents.google.com/patent/US6804573)), qui
décrit l'extension du nœud terminal du squelette « dans la direction de la
branche squelettique entrante », proportionnellement à l'épaisseur locale, pour
que le tracé final couvre l'embout entier d'un objet fin. Le même brevet motive
deux pistes non retenues dans ce lot (portée limitée à la couverture des bouts) :
la classification fin/épais par statistiques de transformée de distance
(max/moyenne/écart-type le long du squelette, plus riche que nos seuils actuels
largeur min/max) et l'ancrage des jonctions sur les concavités réelles du
contour plutôt que sur la seule topologie du squelette. Voir aussi Bai, Latecki
& Liu, *« Skeleton Pruning by Contour Partitioning with Discrete Curve
Evolution »*, IEEE TPAMI 2007
([PDF](https://cis.temple.edu/~latecki/Papers/skeletonPAMI06.pdf)) pour un
élagage de squelette plus fidèle à la forme que notre seuil longueur/rayon
actuel (`graph_cleanup.cpp`) — piste de travail future, non implémentée ici.

### Ancrage des jonctions sur les sommets reflex du contour

*État : Présent · Testé numériquement · Validé visuellement (SVG).* Activé par
défaut (`SatinColumnsParameters::anchor_junction_ends`), rétrocompatible.

**Défaut trouvé par revue** (suite de la mission « auto-satin béton », piste
« ancrage des jonctions » du même brevet). Mesuré précisément sur `y` : la
largeur d'une branche reste stable (~5,0 mm) loin de sa jonction, mais **dérive
nettement** sur les toutes dernières stations avant le nœud du squelette —
jusqu'à ~9,2 mm sur la dernière station de la branche principale, une
croissance quasi linéaire station après station. Cause : la section
transversale d'une branche est calculée depuis sa **seule tangente locale** ;
près d'une confluence, plusieurs branches se recouvrent physiquement, et le
rayon perpendiculaire balaie alors ce **bourrelet de la confluence** — pas la
ceinture réelle de cette branche seule. Conséquence démontrée : les 3 sections
d'un Y ne se rejoignaient PAS au même point — jusqu'à **~5 mm d'écart** entre
deux rails censés coïncider exactement à la confluence, laissant un vide ou un
repli selon les branches.

**Correction** (`trim_and_anchor_junction_end` dans `satin_column.cpp`) en
deux temps, pour chaque bout de JONCTION (jamais un bout ouvert — traité par
l'extension des bouts ouverts, § précédente) :

1. **Amputation** de la queue instable : les stations terminales sont retirées
   tant que leur largeur dépasse de plus de 10 % celle de leur voisine plus
   intérieure — signature précise de la dérive mesurée (croissance nette et
   soudaine, à distinguer d'une variation progressive légitime ailleurs dans
   la colonne, jamais touchée par cette règle).
2. **Ancrage** indépendant de chaque rail sur le **sommet reflex** (concave) du
   contour le plus proche, dans un rayon borné
   (`junction_anchor_radius`, 6 mm par défaut) : les sommets reflex sont
   exactement les « encoches » où le contour réel bascule d'une branche à sa
   voisine. Les deux rails d'une même branche touchent généralement **deux
   encoches distinctes** — jamais ancrés au même point. Si les deux rails
   convoitent le même sommet (cas réel trouvé sur un « T » où une moitié de
   barre n'a qu'un seul côté avec une vraie encoche voisine), seul le plus
   proche le garde ; l'autre cherche son propre sommet **distinct** ou renonce
   — sans cette règle d'exclusion, les deux rails convergeaient par erreur
   vers l'unique encoche, créant un barreau de largeur nulle. Sans ancre à
   portée, un rail conserve sa position amputée (repli sans erreur — toutes
   les jonctions ne sont pas des étoiles symétriques à *n* encoches pour *n*
   branches).

Vérifié : sur un Y symétrique (3 branches), les 6 extrémités de rail touchant
la jonction se regroupent en **exactement 3 sommets, chacun partagé par
exactement 2 rails** de sections différentes (`length_um` < 5 µm entre les
deux) ; sur un T (topologie asymétrique, 2 encoches réelles pour 3 branches),
**aucune collision** (jamais 3 rails ou plus au même sommet) et aucun barreau
dégénéré ; aucune dérive de largeur résiduelle (> 1,2× la médiane) sur aucune
section ; déterminisme ; le bascule `anchor_junction_ends=false` restaure
l'ancienne dérive (régression volontairement reproduite pour prouver que le
bascule agit réellement).

### Amas de pixels de jonction et ancrage indépendant par branche (audit jonctions branchées/concaves, 2026-08-03)

*État : Présent · Testé numériquement.* Correctif de revue,
`skeleton_graph.cpp` + `satin_column.cpp`.

**Défaut trouvé par revue** (capture d'une forme branchée/concave réelle,
suite de la mission « ancrage des jonctions » ci-dessus). Deux causes
distinctes, une même conséquence visible : les rails venant de deux branches
angulairement voisines d'une même confluence ne se raccordent pas exactement
— derniers barreaux en éventail, rails terminaux qui ne partagent pas le même
point, zone triangulaire mal couverte près du centre.

1. **`build_skeleton_graph` ne consolidait pas les amas de pixels de
   jonction.** Sur une confluence à 3+ branches — surtout asymétrique —,
   l'amincissement Zhang-Suen laisse souvent un petit amas de PLUSIEURS
   pixels adjacents ayant chacun un nombre de croisement ≥ 3, plutôt qu'un
   unique pixel net. L'ancien code créait un `SkeletonNode` **par pixel** de
   cet amas, reliés entre eux par des micro-arêtes internes (`length_um` de
   quelques dizaines de µm) que `graph_cleanup.cpp` ne pouvait pas élaguer
   (son seuil ne s'applique qu'aux arêtes **terminales**, degré 1 — une
   micro-arête entre deux pixels de jonction a ses deux extrémités de degré
   ≥ 3). Chaque branche incidente se retrouvait donc ancrée sur un nœud
   légèrement différent selon le pixel de l'amas par lequel son tracé y
   entrait.
2. **`trim_and_anchor_junction_end` ancrait chaque rail indépendamment**, y
   compris après la consolidation ci-dessus : chaque rail de chaque branche
   cherchait pour son propre compte le sommet reflex le plus proche
   (`reflex_vertices`, liste globale du contour), sans aucune coordination
   avec les rails des branches VOISINES de la même confluence. Deux branches
   angulairement adjacentes pouvaient donc élire des sommets différents pour
   ce qui est géométriquement la MÊME encoche.

**Correction**, deux volets :

1. `build_skeleton_graph` regroupe désormais les pixels de jonction
   8-connexes (union-find) en un seul amas logique avant de créer les
   `SkeletonNode` : position stable = le pixel de plus grand rayon dans le
   `DistanceField` au sein de l'amas (départagé par (y, x) croissants pour le
   déterminisme — toujours un pixel réel, jamais un barycentre flottant),
   toutes les branches incidentes pointent vers ce même identifiant, et toute
   micro-arête interne à l'amas (`from == to` après consolidation) est
   rejetée sans condition.
2. L'ancrage sur sommet reflex est sorti de `build_column` (qui ne fait plus
   que l'AMPUTATION de la queue instable, `trim_unstable_junction_tail`) et
   devient une résolution **globale par jonction**
   (`resolve_junction_anchors`, `satin_column.cpp`), appelée une fois toutes
   les colonnes construites : les branches incidentes à une jonction sont
   triées par angle autour de son centre, et chaque ESPACE ANGULAIRE entre
   deux branches adjacentes reçoit au plus une ancre (le sommet reflex le
   plus proche du centre dans ce secteur), appliquée **identiquement** aux
   deux rails qui se font face — le rail gauche (A) de la branche « avant »
   dans l'ordre angulaire et le rail droit (B) de la branche « après ».
   Une branche sans encoche à portée dans son secteur garde sa position brute
   (repli, comme avant — toutes les confluences n'ont pas autant d'encoches
   que de branches, cf. le T ci-dessus). Défaut annexe trouvé en cours
   d'audit : la section transversale brute d'une branche rétrécit
   naturellement vers zéro du côté qui longe une encoche (géométrie réelle,
   pas une erreur) ; quand seul le côté OPPOSÉ de cette branche reçoit une
   ancre partagée, le côté non ancré restait à cette position quasi nulle —
   barreau terminal dégénéré. Corrigé par un plancher de largeur
   (`tip_min_width`, même paramètre que la fermeture de `extend_tip`) qui ne
   déplace QUE le côté non ancré, jamais le côté ancré (qui doit rester
   exactement coïncident avec la branche voisine).

Une validation finale par jonction (`validate_junctions`, appelée après la
résolution des ancres) refuse proprement la génération de TOUTE la région
plutôt que de renvoyer un raccord incohérent : arêtes incidentes manquantes
(une branche non construite sur une confluence sinon partiellement ancrée),
rails terminaux qui ne coïncident pas et s'éloignent trop du centre (éventail
non résolu), croisement entre barreaux terminaux de branches voisines, ou
secteur angulaire anormalement large sans aucune ancre commune (trou
triangulaire). `extend_tip` reste structurellement impossible côté jonction
(un bout de jonction n'emprunte jamais ce chemin, inchangé par cet audit).

Vérifié : suite `test_auto_satin` complète inchangée (`y`, `t`, `cross`, `h`
toujours sans collision ni barreau dégénéré) ; nouvelle fixture `trident`
(grande branche verticale épaisse, branche interne pointue — triangle effilé,
pas une bande à largeur constante — et branche latérale étroite, confluence
délibérément non étoilée) : raccord cohérent, aucune collision à 3+ rails,
aucun barreau dégénéré, aucun croisement entre les trois barreaux terminaux,
tous les barreaux dans la région ; déterminisme.

### Remplacement de l'ancrage par des bridges verrouillés, sans mutation de rail (2026-08-03, suite)

*État : Présent · Testé numériquement.* Remplacement architectural,
`satin_column.cpp` + `satin_column.hpp` + `debug_export.cpp`.

**Défaut trouvé en usage réel sur `trident`** (confluence à 3 branches très
inégales — 6 mm / 1,2 mm / pointe effilée) : `resolve_junction_anchors`
(§ ci-dessus) reste correct sur les confluences symétriques ou peu contrastées
(`y`, `t`, `cross`, `h`), mais sur `trident`, l'ancre trouvée pour un espace
angulaire pouvait se situer à plusieurs millimètres de la dernière station
réellement stable de la branche. `set_terminal_point` déplaçait alors le
dernier nœud du rail **en ligne droite** jusqu'à cette ancre — une corde
artificielle traversant l'intérieur de la confluence au lieu de suivre le bord
réel de la forme, visible comme une grande diagonale dans le SVG de
diagnostic. Le même mécanisme (`tip_min_width` plancher, repli symétrique)
souffrait du même défaut de principe : déplacer un point plutôt que construire
la géométrie.

**Correction : suppression complète de la mutation.** `resolve_junction_anchors`,
`best_anchor_in_sector`, `boundary_anchor_on_sector_bisector`,
`set_terminal_point` et `reflex_vertices` sont retirés. Un bout de jonction
n'est plus jamais déplacé après coup : la dernière station stable que
`trim_unstable_junction_tail` (inchangée dans son principe) laisse en place
devient directement le **`JunctionBridge`** verrouillé de la colonne —
`rail_a_point`/`rail_b_point` (déjà des points réels du contour, `cross_section`
les calcule par intersection avec le bord), `axis_point`, `tangent`,
`width_um`. Deux branches angulairement adjacentes ne sont plus forcées à
coïncider : le petit vide géométrique qui peut subsister entre leurs bridges
respectifs (`JunctionCore`, exposé dans `SatinColumnsResult::junction_cores`,
jamais dissimulé) est calculé séparément et n'entraîne PAS de refus — seul un
défaut structurel réel (bridge égaré, croisement) refuse encore la génération
(`validate_junction_bridges`).

Deux effets de bord de la suppression de l'ancrage, trouvés en generalisant
sur `trident` puis sur un réseau en T issu d'une image segmentée réelle :

1. **Le critère de stabilité de `trim_unstable_junction_tail` devait devenir
   plus robuste.** L'ancien plafond de distance (lié à `junction_anchor_radius`,
   qui ne bornait à l'origine que le rayon de recherche d'une ancre — notion
   disparue) arrêtait parfois l'amputation alors que la largeur était encore en
   pleine dérive (bourrelet de `trident` dépassant 6 mm d'abscisse curviligne) ;
   supprimé, plus aucun plafond de distance. Le critère de largeur lui-même
   (décroissance stricte station à station) échouait sur un PALIER de largeur
   (deux stations quasi égales mais toutes deux énormes) — remplacé par une
   comparaison à `representative_station_width` : la médiane du **tiers
   médian** des stations de la branche (ni le premier tiers, qui peut être
   contaminé par un bout OUVERT qui rétrécit légitimement vers une pointe —
   `trident`, branche interne effilée — ni le dernier, potentiellement gonflé
   par le bourrelet de jonction).
2. **Un bridge peut légitimement croiser celui d'une branche voisine sans
   dérive de largeur mesurable**, si l'angle entre les deux branches est
   proche de 90° et que l'une d'elles est nettement plus large que l'autre (la
   coupe transversale, perpendiculaire à la branche fine, atteint alors le
   bord de la branche large sans que sa PROPRE largeur ait significativement
   augmenté). `resolve_and_validate_junctions` recule alors itérativement
   d'une station RÉELLE supplémentaire (`retract_bridge_station`, jamais un
   déplacement) sur les deux branches en conflit, jusqu'à ce que le croisement
   disparaisse ou que le plancher de stations soit atteint.

Nouvelles primitives internes (diagnostic uniquement, jamais utilisées pour
construire ou déplacer un rail) : `ContourPolyline`/`project_to_contour`/
`extract_contour_arc` (polygone fermé, abscisse curviligne, projection,
extraction d'arc) — utilisées par `compute_junction_core` pour approximer le
bord réel du noyau central entre deux bridges, et par `columns_to_svg` pour
l'afficher (remplissage magenta pointillé) aux côtés des bridges terminaux
(barreaux rouges épais) et des identifiants de colonne/jonction.

Vérifié : suite `test_auto_satin` complète (44 cas) ; nouveau test dédié
`trident` sur la géométrie des rails (aucun segment terminal excédant 4×
`station_spacing`, progression curviligne strictement monotone,
`junction_cores` non vide et borné) ; test `y` symétrique réécrit (l'ancienne
coïncidence exacte forcée n'est plus un invariant recherché — remplacé par
« bridges locaux au centre de confluence, noyau résiduel petit ») ; suite
`test_autodigitize` (réseau en T issu d'image segmentée, régression trouvée et
corrigée par la retractation itérative ci-dessus) ; suite d'intégration
complète (`tentabrode`) ; déterminisme.

**Limite connue** : la décomposition d'une branche en sous-région fermée
propre (rail A + bridge + rail B inversé + autre bridge, comme dans Ink/Stitch)
et le remplissage explicite du `JunctionCore` par un objet tatami distinct ne
sont pas implémentés — le noyau reste un diagnostic (aire + contour approximatif)
non cousu. Les corps de branche restent construits par échantillonnage de
sections transversales le long de l'axe du squelette (inchangé), pas par une
paire d'arcs de contour indépendants.

### Optimisation globale des bridges de jonction + correctif d'un polygone auto-croisé (suite)

*État : Présent · Testé numériquement.* Remplacement de `resolve_
junction_anchors`/`retract_bridge_station` par une recherche combinatoire,
`satin_column.cpp` + `satin_column.hpp` + `debug_export.cpp`.

**Défaut signalé en usage réel sur `trident`** : la capture indépendante « une
seule candidate par branche » (§ ci-dessus) choisissait toujours la toute
dernière station de chaque branche comme bridge, sans coordination avec ses
voisines — sur une confluence aussi asymétrique que `trident`, ces bridges
restaient parfois francs millimètres les uns des autres, et le `JunctionCore`
qui en résultait apparaissait comme un grand polygone concave recouvrant
toute la confluence.

**Deux défauts distincts corrigés, pas un seul** :

1. **Optimisation combinatoire des bridges (la demande initiale).** Chaque
   branche conserve désormais plusieurs sections transversales CANDIDATES
   (`collect_bridge_candidates`) — des stations déjà présentes dans son rail,
   jamais un point fabriqué — prises dans un rayon local
   (`junction_core_radius`, nouveau paramètre, 4 mm par défaut) autour du nœud
   de squelette. `optimize_junction_bridges` évalue toutes les combinaisons
   raisonnables (une candidate par branche, budget total borné quel que soit
   le degré de la jonction) et retient celle de coût minimal — aire du
   `JunctionCore`, distance maximale au centre, recul total le long des
   branches, discontinuités de largeur — parmi celles qui interdisent tout
   croisement bridge/bridge (`find_crossing_bridge_pair`) et toute traversée
   d'une branche voisine (`bridge_crosses_other_rail`). **Aucun rail n'est
   modifié** : seul l'index de la station retenue varie d'une combinaison à
   l'autre. `retract_bridge_station` (qui tronquait réellement les rails) est
   supprimé — l'invariant « les rails restent ceux produits par
   `build_column` » est désormais strict, plus seulement documenté.

2. **Bug préexistant trouvé EN OPTIMISANT, indépendant du point 1 : l'ordre
   des sommets de `compute_junction_core` reliait les mauvais côtés.** `rail_a`
   (+N) n'est PAS toujours le côté qui fait face à la branche suivante dans
   l'ordre angulaire : sa convention gauche/droite est relative au sens de
   parcours interne du rail (`build_column`), qui va tantôt de la jonction
   vers le bout ouvert, tantôt l'inverse, selon l'orientation de l'arête de
   squelette (`atEnd`) — alors que `tangent` (`outward_tangent`) pointe
   toujours correctement vers l'intérieur de la branche, quel que soit
   `atEnd`. L'ancien code connectait systématiquement `rail_b(cur)` à
   `rail_a(next)` : sur une branche où `atEnd` inverse la convention (le cas
   des deux bras courts de `trident`), ce n'était PAS le côté qui fait
   réellement face au voisin. L'arc de contour « le plus court » entre deux
   points qui ne se font pas face part alors dans une direction non
   pertinente, et le polygone assemblé s'auto-croise — la formule du lacet
   annule alors une partie de l'aire réelle par recouvrement, produisant un
   nombre artificiellement PETIT (le fameux 3,24 mm² observé) pour un
   polygone géométriquement INVALIDE. Corrigé par un calcul explicite,
   `leading_point`/`trailing_point`, dérivé de `tangent` tourné de +90°
   (fiable quel que soit `atEnd`) plutôt que des labels `rail_a`/`rail_b` bruts.

**Garde-fous ajoutés une fois le bug de connexion corrigé** :
`polygon_self_intersects` détecte tout polygone auto-croisé restant ;
`compute_junction_core` construit désormais le noyau en DEUX PASSES — d'abord
la version repli (segments droits uniquement entre bridges successifs ; si
elle-même s'auto-croise, la combinaison entière est invalide) puis, pour
chaque connecteur, un remplacement par l'arc de contour réel UNIQUEMENT s'il
garde le polygone simple ET ne fait pas grossir son aire (sur une confluence
où 3+ branches se chevauchent mutuellement, l'arc « le plus court » au sens
de la longueur de contour peut rester simple tout en empruntant un grand
détour convexe).

**Résultat mesuré** : une fois le polygone correctement calculé (simple, non
auto-croisé), l'aire réelle du noyau de `trident` est de l'ordre de 22 à
23,5 mm² selon les seuils — **plus grande**, pas plus petite, que l'ancien
3,24 mm² invalide. Ceci n'est PAS une régression : `y`, `t`, `cross` et `h`
donnent des noyaux carrés/triangulaires parfaitement simples de 20 à 30 mm²
une fois rendus en SVG et inspectés visuellement (bandes de test larges de
5 mm, donc recouvrement mutuel substantiel aux confluences par construction),
confirmant que ces ordres de grandeur sont réalistes pour ces fixtures, pas
un artefact. `JunctionCore::requires_fill` (nouveau champ, seuil
`junction_core_significant_area_um2` = 0,5 mm² par défaut) signale qu'un
noyau de cette taille appelle un objet de remplissage séparé plutôt qu'une
zone non couturée silencieuse — la synthèse de cet objet reste hors périmètre
(diagnostic seul, cf. limite connue ci-dessus).

Diagnostic enrichi : `SatinColumnsResult::junction_bridge_candidates`
(nouveau, toutes les candidates évaluées par branche + laquelle est retenue)
et `JunctionCore::radius_um` sont exposés et affichés par `columns_to_svg`
(candidates en gris pointillé fin, bridge retenu en vert, disque de
recherche/contenance en pointillé gris autour du nœud de squelette).

Vérifié : suite `test_auto_satin` complète (44 cas, déterminisme inclus) ;
test `trident` étendu (rails inchangés — déterminisme + chaque nœud à moins de
0,2 mm du contour réel —, noyau local à J1 au rayon `core.radius_um` près,
aire bornée à une valeur réaliste et documentée comme telle) ; inspection
visuelle des SVG rendus (`y`, `cross`, `h`, `trident`) confirmant des noyaux
simples, bien formés, sans auto-croisement ni grand polygone parasite.

### Séparation StableBranchEnd / JunctionSeparator : le noyau n'est plus « région entière moins colonnes » (suite)

*État : Présent · Testé numériquement.* Remplacement de l'optimisation
combinatoire de bridges (§ ci-dessus) par une partition explicite,
`satin_column.cpp` + `satin_column.hpp` + `debug_export.cpp`.

**Défaut architectural signalé en usage réel sur `trident`** : la recherche
combinatoire (§ précédente) choisit, PARMI plusieurs reculs candidats d'une
même branche, celui qui minimise l'aire du `JunctionCore` — mais reculer une
section transversale ne change pas où se trouve la vraie frontière
géométrique entre deux branches (l'encoche réelle du contour). Sur `trident`,
aucune combinaison de reculs ne pouvait donc produire un noyau petit ; le
résultat mesuré (22 à 23,5 mm², quasiment toute la confluence) était réel,
pas un artefact — mais architecturalement le mauvais résultat.

**Correction : séparer deux notions jusqu'ici confondues.**

1. **`StableBranchEnd`** — la dernière section transversale RÉELLEMENT
   stable d'une branche (ce qu'on appelait le « bridge »), inchangée dans son
   principe (`trim_unstable_junction_tail`) : point d'entrée du traitement de
   jonction, jamais déplacée.
2. **`JunctionSeparator`** — la frontière locale PARTAGÉE entre deux branches
   angulairement adjacentes, construite DANS la zone de confluence (sommet
   reflex du contour le plus profond, ou repli sur le milieu curviligne de
   l'arc de contour reliant les deux branches) : ce n'est ni une section
   transversale, ni jamais un point sur un rail.

La région locale de jonction (disque de rayon adaptatif — `1,2 ×` la plus
éloignée des `StableBranchEnd` au nœud, jamais une constante fixe, plafonné
par `junction_core_radius`) est partitionnée en un secteur par branche
(bordé par ses deux `JunctionSeparator` voisins, l'arc de contour réel, et sa
propre `StableBranchEnd`). Le `JunctionCore` résiduel est calculé **comme
`local_region − union(secteurs)`**, jamais comme « région entière moins
colonnes » : concrètement, le polygone des `JunctionSeparator` eux-mêmes,
simple par construction (sommets triés par angle autour d'un centre commun =
polygone en étoile).

**Deux bugs trouvés en généralisant, tous deux avec un symptôme identique
(noyau anormalement grand) mais des causes distinctes :**

1. **Recherche de séparateur par SECTEUR ANGULAIRE au lieu de CONTIGUÏTÉ DE
   CONTOUR.** La première implémentation cherchait le sommet reflex d'un
   espace angulaire `[angle(branche i), angle(branche i+1)]`, basé sur
   l'angle du rayon squelette de chaque branche. Sur `trident`, la branche
   verticale (6 mm) a ses points `leading`/`trailing` écartés de plus de 90°
   autour du nœud : l'encoche réelle entre elle et sa voisine fine tombait
   alors, en angle pur, dans le secteur voisin — laissant DEUX des trois
   espaces sans aucun candidat, repliés sur un rayon lancé depuis le nœud qui
   pouvait heurter le mur d'une branche sans rapport (noyau mesuré : 14,4 mm²,
   toujours trop grand). Corrigé en cherchant plutôt sur l'arc de contour qui
   relie réellement `leading_point(branche i)` à `trailing_point(branche
   i+1)` (`ContourDirection::Shortest`) : la contiguïté de contour reflète la
   vraie adjacence géométrique, pas l'angle depuis un centre qui peut être
   trompeur pour une branche large. Résultat après correction : 0,94 mm².
2. **Une `StableBranchEnd` peut être stable en LARGEUR tout en restant
   géométriquement dans le corridor d'une voisine.** `trim_unstable_junction_tail`
   ne détecte que la dérive de largeur du bourrelet — pas sa position. Sur un
   réseau en T très asymétrique (bras large, branche fine, régression trouvée
   par `test_autodigitize`), la toute première section d'un bras pouvait avoir
   une largeur déjà plausible (donc jamais amputée) tout en croisant le rail
   de la branche voisine, un défaut que le seul critère de largeur ne peut pas
   voir. Corrigé par une retractation itérative bornée dans `resolve_junction`
   (jamais dans `build_column`) : toute paire de `StableBranchEnd` dont les
   coupes se croisent (`find_crossing_end_pair`/`end_crosses_other_rail`) fait
   reculer les deux branches impliquées d'une station réelle supplémentaire
   (`make_stable_branch_end` généralisé avec un paramètre `offset`), jusqu'à
   ce que la confluence soit cohérente ou qu'il n'y ait plus de station
   disponible (refus propre, comme avant).

**Diagnostic enrichi** : `SatinColumnsResult` expose désormais
`stable_branch_ends`, `junction_separators` et `junction_sectors` (en plus de
`junction_cores`, dont les champs `radius_um` unique est remplacé par
`configured_radius_um`/`local_radius_um`/`actual_max_radius_um` — le premier
est un plafond de sécurité, jamais le rayon réel). `columns_to_svg` affiche
les `StableBranchEnd` en rouge, les `JunctionSeparator` en vert, un secteur
par branche en teinte translucide distincte, le noyau en magenta, et le
disque de région locale en pointillé — avec les trois rayons étiquetés
séparément pour ne plus jamais laisser croire que le plafond configuré est
une mesure.

Vérifié : suite `test_auto_satin` complète (44 cas) ; test `trident` étendu
(3 secteurs, 3 séparateurs partagés exactement entre secteurs adjacents,
aire du noyau très inférieure à l'ancien résultat invalide, aucun point du
noyau au-delà du rayon local réellement calculé) ; suite `test_autodigitize`
complète (réseau en T, régression trouvée et corrigée par la retractation
itérative ci-dessus) ; inspection visuelle des SVG rendus (`trident`, `y`,
`t`, `cross`, `h`) : noyaux petits et centrés sur `trident`/`y` (encoches
réelles présentes), noyaux carrés honnêtes sur `t`/`cross`/`h` (branches de
même largeur se croisant à angle droit sans aucune encoche — un noyau non
trivial y est le résultat géométriquement correct, pas un défaut).

### Traçage du graphe de squelette : priorité aux nœuds, exclusion de l'origine

*État : Présent · Testé numériquement.* Correctif de revue, `skeleton_graph.cpp`.

**Défaut trouvé par revue** (deux symptômes distincts, même cause racine). Le
traçage d'une arête (`build_skeleton_graph`) suit une chaîne de pixels de
degré 2 depuis un pixel-nœud jusqu'au prochain pixel-nœud, en choisissant à
chaque pas le **prochain voisin** parmi les 8 directions. L'ancien code
s'arrêtait sur le **premier candidat rencontré** dans l'ordre fixe des 8
directions, qu'il s'agisse d'un vrai nœud ou d'un simple pixel de
continuation — alors qu'un pixel juste avant une jonction a souvent, en plus
de la jonction elle-même, un pixel de degré 2 d'une **autre branche** dans
son 8-voisinage (les branches se recouvrent physiquement près d'une
confluence, cf. section précédente).

1. **Jonction "croix" ramenée à un degré 2 au lieu de 4** (le défaut
   initialement identifié, stable sur toutes les résolutions testées) : deux
   bras opposés étaient fusionnés en une seule arête traversante quand le
   pixel de l'un d'eux apparaissait avant le pixel de la jonction elle-même
   dans l'ordre de balayage.
2. **Branche entière perdue sur un réseau "y" avec arête parasite en boucle**
   (défaut distinct, non documenté avant cette revue) : le pixel d'**origine**
   de la trace pouvait être ré-atteint par un chemin de 2 pas différent de
   celui emprunté au départ — seul le pixel immédiatement précédent était
   exclu des candidats, pas le pixel d'origine de toute la trace. La tige du
   "y" (une branche entière) disparaissait alors du graphe, remplacée par une
   arête `from == to` de quelques centaines de µm sur la jonction, sans aucune
   arête ni diagnostic pour l'utilisateur.

**Correction** : priorité **absolue** à un nœud sur tout le 8-voisinage
(balayage complet des 8 directions pour un nœud avant de retomber sur un
pixel de continuation, au lieu du premier candidat rencontré), et exclusion
du pixel d'**origine** de la trace (pas seulement le pixel précédent) pendant
toute la marche.

Vérifié : la jonction d'une `cross` a désormais un degré réel de 4 dans le
graphe élagué (4 arêtes, 4 extrémités, 1 jonction) ; un réseau `y` conserve
ses 3 branches (aucune arête `from == to`) ; aucune régression sur les formes
déjà testées (`rectangle`, `t`, `capsule`, `ribbon`, `s`, `ring`, `wide`,
`circle`).

### Contrat de `graph_cleanup.cpp` : aucune suppression silencieuse

*État : Présent · Testé numériquement.* Correctif de revue.

**Défaut trouvé par revue** : `graph_cleanup.hpp` documente explicitement
« ne supprime rien silencieusement (chaque suppression est diagnostiquée) »,
mais `prune_graph` ne réinsérait un nœud dans le graphe élagué que s'il était
référencé par au moins une arête vivante — un nœud du graphe brut **sans
aucune arête** (isolé dès le départ, comme le point unique auquel se réduit
le squelette d'un disque plein, ou devenu orphelin après élagage d'une arête
terminale courte) disparaissait donc du résultat sans jamais apparaître dans
`removed`, contredisant le contrat documenté.

**Correction** : tout nœud du graphe d'origine non repris dans le graphe
élagué (aucune arête vivante incidente, quelle qu'en soit la raison) est
désormais systématiquement ajouté à `removed`, avec un motif explicite.
Vérifié sur `circle` (squelette réduit à un point isolé) : le graphe élagué
reste vide, mais le nœud isolé apparaît dans le diagnostic.

### Arête Jonction-Jonction (pont d'un "H") convertie en colonne

*État : Présent · Testé numériquement.* Correctif de revue, `satin_column.cpp`.

**Défaut trouvé par revue** : `build_satin_columns` (cas `RequiresDecomposition`)
ne convertissait en colonne que les arêtes du squelette élagué touchant au
moins une **extrémité** (« une colonne par branche menant à une extrémité, bras
du Y/T »). Une arête reliant **deux jonctions** — le pont horizontal d'un "H",
topologie absente des formes de test précédentes (Y/T/croix n'ont qu'une seule
jonction) — n'était donc jamais essayée : `r.columns` restait non vide (les
bras des deux barres verticales étaient bien produits) et `r.refusal` restait
vide (aucun signal d'erreur), mais le pont lui-même — une portion entière et
visible de la région — ne recevait **aucun point de broderie**.

**Correction** : toutes les arêtes du graphe élagué sont désormais essayées,
sans distinction sur le type de leurs deux extrémités — `try_edge` gère déjà
chaque bout indépendamment selon son type (extension si extrémité ouverte,
ancrage si jonction), aucune branche de code particulière n'était nécessaire.
Vérifié sur une fixture "H" dédiée (deux barres verticales de 40 mm reliées
par un pont horizontal de 5 mm) : 5 colonnes produites (les 4 demi-barres +
le pont), aucune collision aux deux jonctions, aucun barreau dégénéré.

### Amputation de la queue instable : décroissance stricte, pas un seuil fixe

*État : Présent · Testé numériquement.* Correctif de revue, `satin_column.cpp`.

**Défaut trouvé par revue**, révélé par le correctif de traçage ci-dessus (la
branche "y" auparavant perdue expose maintenant ce second défaut, jusque-là
invisible faute d'atteindre `satin_column.cpp`). L'étape 1 de l'ancrage des
jonctions (§ précédente) amputait la queue instable en comparant chaque
station à sa **seule voisine immédiate** (seuil relatif fixe, +10 %) : ce
critère suppose que le bourrelet de confluence retombe en un seul saut net.
Mesuré sur la tige du "y" : la largeur décroît **progressivement** sur
plusieurs stations (9200 → 8586 → 7576 → 6566 → 5554 → 5000 µm, stable),
où **chaque écart pris isolément reste sous 10 %** (le premier, 9200 vs 8586,
n'est que 7 %) mais la dérive cumulée atteint +72 % — l'ancien critère
s'arrêtait dès le tout premier écart local insuffisant, laissant la
quasi-totalité du bourrelet en place.

**Correction** : le critère devient une décroissance **stricte** (avec une
tolérance de 1 µm pour le bruit flottant) au lieu d'un seuil relatif fixe —
toute décroissance signale qu'on est encore dans la zone d'influence de la
confluence, sans hypothèse sur l'ampleur du saut d'une station à l'autre.
Vérifié : la largeur de la tige du "y" (résolution 0,1 mm, où les 3 branches
survivent désormais grâce au correctif de traçage) reste sous 1,2× la médiane
sur toutes les stations, y compris celles immédiatement après la jonction ;
aucune régression sur les cas déjà couverts (Y symétrique à 50 µm, T).

Décision : `Suitable` → une colonne (axe principal) ; `RequiresDecomposition`
(Y/T) → une colonne par branche menant à une extrémité. Un anneau fin à un
trou est ouvert sur une couture déterministe puis décomposé en quatre sections
cycliques ; ses barreaux sont contrôlés sur plusieurs échantillons et contre
les croisements locaux/globaux. `Ambiguous` (quasi circulaire sans trou) et les
formes trop larges/étroites ou non appariables → **refus explicite**.
Déterministe. Vérifié visuellement via `openstitch-cli auto-satin-debug --shape
<forme> --output-svg` (SVG dans `tests/golden/auto-satin/`).

Intégration : la numérisation automatique et **Broderie ▸ Convertir
automatiquement en satin…** créent une `EmbroideryObject` par section. Toutes
les sections d'un réseau gardent la même source/couleur ; le routage Lot 6 les
traite donc comme un groupe multi-rail. Chaque objet reste un `SatinParams`
historique à deux rails : aucune rupture du format. Les **barreaux sont stockés**
dans `SatinParams.rungs` et sérialisés (`.osp` schéma v2, rétrocompatible).

Chaque section générée porte désormais une topologie explicite et déterministe :
`section_index`/`section_count`, plus `start_junction` et `end_junction` quand
l'extrémité touche une jonction du squelette. Les anneaux forment un cycle de
quatre jonctions ; les réseaux Y/T réutilisent le même identifiant au point
partagé. Ces métadonnées deviennent le bloc `SatinParams.topology` optionnel
dans le document et le `.osp`. Un ancien satin sans ce bloc reste une colonne
isolée. Ce lot fournit une identité de jonction fiable ; il ne modifie pas encore
le routage ni l'édition coordonnée des guides.

Régressions couvertes : bande simple, réseau en T (au moins trois sections),
anneau rasterisé (quatre sections, trou préservé) et pipeline complet sur
`tests/fixtures/tentabrode.png`, en Debug et Release. Cette validation reste
logicielle ; la qualité textile et les cas très concaves demandent encore un
contrôle visuel puis machine.

### Génération partielle sur formes concaves : trous silencieux dans `build_column`

*État : Présent · Testé numériquement.* Correctif de revue, `satin_column.cpp`
(audit 2026-08-03).

**Défaut trouvé par revue.** `build_column` échantillonne l'axe (squelette
lissé) station par station et calcule à chaque point une section transversale
(`cross_section`, intersection de la normale locale avec le contour). Sur une
forme **concave ou très large**, cette section transversale peut légitimement
échouer pour certaines stations (normale qui rase une encoche voisine, section
plus large que `max_satin_width`, milieu de l'intervalle retombant hors
région) — un cas déjà connu et documenté (§ *Rails automatiques* plus haut, à
propos de l'heuristique naïve). Le code de `build_column`, lui, traitait cet
échec par un simple `continue` : la station disparaissait de l'axe **sans
aucune trace**, et les deux stations valides encadrant le trou se retrouvaient
reconnectées directement, quelle que soit la distance réelle entre elles. Le
nettoyage anti-croisement qui suit (retrait d'une station dont le quadrilatère
avec la précédente s'auto-intersecte) souffrait du même défaut : les stations
retirées n'étaient jamais comptées ni bornées. Sur un projet réel présentant
une échancrure prononcée, ceci produisait — selon la forme — de larges zones
de la région sans aucun point de broderie, des rails visiblement discontinus,
des éventails ou pointes artificielles à l'endroit de la reconnexion, ou une
colonne partiellement construite là où un refus propre aurait été attendu.

**Correction**, entièrement dans `build_column` (le squelette Zhang-Suen et le
graphe de squelette ne sont pas en cause — le défaut est postérieur, dans la
conversion centerline → rails) :

1. `cross_section` retourne désormais un `std::expected<..., CrossSectionFailure>`
   au lieu d'un `std::optional` indifférencié : la raison exacte de l'échec
   (axe hors région, intersection négative/positive manquante, largeur
   supérieure à `max_satin_width`, intervalle invalide) est conservée et
   propagée jusqu'aux diagnostics (`SatinColumnsResult::warnings`).
2. Chaque échantillon d'axe garde son indice et son résultat (succès ou
   échec) avant tout filtrage. Un échec **isolé** (une seule station,
   encadrée de deux succès) est comblé par **interpolation linéaire** des
   rails voisins, acceptée seulement si le résultat reste géométriquement
   plausible (intérieur à la région, largeur non aberrante, aucun croisement
   avec les deux stations voisines) — sinon il est traité comme un trou
   irrésolu. Des échecs en tête ou en queue d'axe restent un bord légitime
   (comportement inchangé, absorbé silencieusement comme avant : ce n'est pas
   un trou interne).
3. Tout trou interne non résolu par interpolation (échecs consécutifs, ou
   isolé mais géométriquement invalide) **refuse la colonne entière**
   plutôt que de la reconnecter à travers le trou. Le nettoyage anti-croisement
   applique le même principe : retirer une ou quelques stations sur un virage
   serré reste toléré (comportement préexistant, nécessaire sur `ribbon`/`y`),
   mais si la distance d'axe réellement pontée dépasse
   `max_station_gap_ratio × station_spacing`, la colonne est refusée.
4. Parce qu'un trou interne irrésolu refuse systématiquement la colonne plutôt
   que de conserver le plus long fragment, `st.front()`/`st.back()` restent
   toujours de vrais bouts d'axe au moment d'appeler `extend_tip` : aucune
   extrémité artificielle issue d'une coupure interne n'est jamais étendue
   comme s'il s'agissait d'un vrai bout du squelette.
5. Une validation finale (sur le cœur de l'axe, **avant** extension des bouts
   et ancrage de jonction — ces deux étapes ont leurs propres tolérances déjà
   testées, notamment le saut de largeur délibéré du point de fermeture
   `tip_min_width`) vérifie l'absence de trou résiduel, l'absence de saut de
   largeur incohérent entre stations adjacentes, et une couverture minimale
   (`min_axis_coverage_ratio`) de la longueur d'axe réellement convertie en
   stations. Une dernière passe, une fois les barreaux posés, rejette tout
   barreau retombant hors région ou toute paire de barreaux qui se croisent.

Calibration : les trois nouveaux seuils (`max_station_gap_ratio` = 5,0 ×
`station_spacing`, `min_axis_coverage_ratio` = 85 %, `max_adjacent_width_jump_ratio`
= 75 %) ont été choisis pour ne régresser aucune fixture existante — `ribbon`
et `y`, sur des virages serrés, écartent normalement 1 à 3 stations d'affilée
lors du nettoyage anti-croisement, et une jonction à haut degré (`cross`, `h`)
peut légitimement voir sa couverture chuter autour de 80 % tout près du nœud du
squelette (bourrelet de confluence, cf. *Ancrage des jonctions* plus haut).

Vérifié : suite complète `test_auto_satin` inchangée (aucune régression sur
`rectangle`, `capsule`, `ribbon`, `s`, `y`, `t`, `cross`, `h`, `ring`, `wide`) ;
nouvelle fixture `notch` (bande de 40 × 5 mm entaillée d'une encoche en V
profonde sur un bord, resserrant la largeur locale à 1 mm) et sa variante plus
sévère `pinch` (resserrement à 0,3 mm) produisent, à chaque exécution, **soit**
un refus explicite (`refusal` non vide, aucune colonne), **soit** une colonne
complète sans trou entre stations consécutives, avec tous les barreaux dans la
région et aucun croisement entre barreaux — jamais un résultat partiel ;
déterminisme vérifié (mêmes rails à chaque exécution sur les deux fixtures).

### Pointes de colonne réduites à quelques dizaines de µm : plancher franchi en un seul pas dans `extend_tip`

*État : Présent · Testé numériquement (unitaire + intégration sur projet réel).*
Correctif d'audit (2026-08-11), retour utilisateur : *« le satin fait
n'importe quoi »* sur un logo circulaire à motif de circuit imprimé
finement détaillé (nombreuses formes fines à extrémité OUVERTE — pointe de
piste, plot de connecteur).

**Défaut trouvé en investiguant le retour utilisateur.** `extend_tip`
prolonge un bout ouvert de colonne au-delà de ce que le squelette aminci
couvre, en marchant par pas de `stepLen` le long de la tangente sortante et
en ré-échantillonnant une section transversale à chaque pas, tant que la
largeur rétrécit. La boucle **poussait la station dans `ext` avant de
vérifier** si sa largeur avait déjà atteint le plancher voulu
(`tip_min_width`) :

```cpp
Station st;
st.width = width;
ext.push_back(st);        // ← poussée d'abord...
...
if (width <= tipMinWidth) {
    return ext;            // ← ...le plancher n'est vérifié qu'ensuite
}
```

Or rien ne borne la largeur **au-dessus** du plancher au moment de la
poussée : si un pas de marche fait chuter la largeur de, disons, 200 µm à
34 µm en une seule itération (taper serré, pas de marche relativement
grossier par rapport à la vitesse de rétrécissement — le cas courant pour
une petite forme pointue), c'est cette station à 34 µm qui se retrouve
poussée dans `ext`, **avant** que la condition d'arrêt n'ait la main. La
bissection de fermeture qui suit (laquelle, elle, respecte correctement
`tipMinWidth` : `half = max(tipMinWidth * 0.5, 1.0)`) n'était alors jamais
atteinte pour cette extrémité — sa station de tête restait celle,
sous-plancher, déjà poussée.

Un second facteur amplifiait la fréquence du défaut sans en changer la
nature : `tip_min_width` valait **50 µm par défaut**, sous le pas de
quantification DST lui-même (0,1 mm, cf. `formats/dst.hpp`) — un barreau à
tout juste 50 µm n'aurait de toute façon jamais représenté un point cousu
exploitable, même sans le défaut d'ordre de poussée ci-dessus.

**Impact mesuré sur un projet réel** (logo circulaire, ~340 régions
segmentées, `tests/integration/test_pipeline.cpp`, diagnostic
« satin pointes fines (GISTRE) ») : **178 des 190 colonnes satin (94 %)**
avaient au moins un barreau sous 0,8 mm, la plupart entre 33 et 51 µm —
quasiment toute colonne à extrémité ouverte était concernée, puisque
`extend_tip` s'applique à chacune. Une colonne satin dont le dernier
barreau mesure quelques dizaines de µm génère, selon la machine et le fil,
un point quasi nul, un bourrage de fil ou une casse — exactement le
symptôme rapporté (*« ça fait n'importe quoi »*), et son omniprésence sur
ce type d'image (nombreuses petites formes en pointe) explique pourquoi il
n'était pas passé inaperçu plus tôt sur des images plus simples (peu de
bouts ouverts fins).

**Correction, deux volets indépendants** :

1. **Ordre de la boucle inversé** : la largeur est comparée au plancher
   *avant* de pousser la station, pas après. Une station déjà sous le
   plancher n'est jamais poussée — la marche s'arrête un pas plus tôt et
   laisse la bissection de fermeture (déjà correcte) fermer proprement au
   plancher exact.
2. **`tip_min_width` porté de 50 à 300 µm** : reste nettement sous
   `min_satin_width` (0,8 mm, le seuil « satin digne de ce nom » pour toute
   la colonne — la pointe reste une pointe, pas un bout à pleine largeur),
   tout en dépassant largement le pas DST avec une marge de sécurité (×3).

Un défaut apparenté, dans la boucle dense principale de
`compute_column_stations` (partagée par `build_column` et
`build_parametric_object`, donc par les deux modes de géométrie) : aucun
plancher de largeur n'existait pour une station de CORPS (pas de bout),
qu'elle vienne de l'axe principal ou d'une interpolation comblant un échec
isolé de `cross_section` (§ *Génération partielle sur formes concaves*
ci-dessus). Une région globalement fine mais localement très étroite sur
une portion de son squelette pouvait ainsi passer le test d'éligibilité
(`satinability.cpp` compare `max_satin_width` à la largeur **maximale**
relevée sur toute la région, pas à sa largeur typique) puis dégénérer en
aval. Ajout d'un nouveau cas d'échec `CrossSectionFailure::TooNarrow`
(largeur sous `min_satin_width`), traité par le mécanisme d'échec déjà en
place (§ *Génération partielle*) : un échec isolé est comblé par
interpolation *si l'interpolée elle-même dépasse le plancher* (`minWidth`
ajouté aux critères de `interpolated_station_valid`, qui ne vérifiait
jusqu'ici que `maxWidth` — sans quoi une station trop étroite se faisait
réaccepter telle quelle par simple interpolation de position avec ses
voisines, sans jamais revalider sa largeur) ; un échec étendu refuse la
colonne entière plutôt que de produire des rails quasi confondus sur toute
sa longueur.

Vérifié : suite `test_auto_satin` complète inchangée (1999 assertions, 51
cas, aucune régression sur les fixtures existantes) ; nouveau cas sur la
géométrie EXACTE d'une région du projet réel en cause
(`tests/unit/auto_satin/test_columns.cpp`) ; diagnostic d'intégration sur
le projet réel complet (`tests/integration/test_pipeline.cpp`, § GISTRE) :
0 barreau sous 0,29 mm après correctif contre 178/190 colonnes touchées
avant, pire séparation observée 299 µm (au plancher voulu, à l'arrondi µm
près).

## Ligne de coupe manuelle pour les jonctions satin (`geometry::cut_path_set`, 2026-08-12)

*État : Présent (outil `DrawSatinCutLine`) · Complément, pas remplacement, de
la détection automatique de jonctions ci-dessus.*

### Origine : audit de la pipeline Ink/Stitch

Sur des logos complexes réels (ex. sceau circulaire à motif de circuit
imprimé), le squelette automatique (`build_satin_columns`) peine parfois sur
des jonctions ambiguës — nœuds en Y/T proches, branches très courtes,
squelette bruité par un contour segmenté imparfaitement. Un audit de la
pipeline auto-satin d'Ink/Stitch (GPL ; documentation publique consultée,
aucun code repris — implémentation ci-dessous entièrement originale) a montré
que cet outil ne tente PAS de détecter les jonctions automatiquement : son
"Stroke to Satin" calcule une ligne centrale puis s'appuie sur des "cut
lines" tracées à la main par l'utilisateur pour séparer une forme aux
endroits voulus, avant conversion en colonnes. Le remplissage tatami sur
formes complexes suit le même principe ("break apart" manuel documenté comme
solution officielle).

Plutôt que de renoncer à la détection automatique (qui reste supérieure sur
la majorité des formes — voir toutes les sections ci-dessus), l'outil de
ligne de coupe est ajouté en complément : un geste manuel simple pour
les cas où l'automatique reste ambigu, sans jamais devenir la voie par
défaut.

### Primitive géométrique : `geometry::cut_path_set`

`libs/geometry/include/openstitch/geometry/cut.hpp` /
`libs/geometry/src/cut.cpp`. Signature :

```cpp
Result<std::vector<PathSet>> cut_path_set(const PathSet& region, Vec2um a, Vec2um b,
                                           Micrometers cut_width = Micrometers{20});
```

Retire une fine bande rectangulaire (`cut_width`, quelques dizaines de µm par
défaut — négligeable une fois cousu) centrée sur le segment `a`-`b`, prolongée
très au-delà de la région dans les deux sens pour garantir une traversée
complète quels que soient les deux points fournis (l'utilisateur n'a donc
qu'à tracer un segment qui *croise* la jonction visée, pas à viser
précisément les bords de la forme). Implémentation via une booléenne Clipper2
`Difference` (même encapsulation par fichier que le reste de `libs/geometry` —
ADR-005 : aucun type Clipper2 ne sort de `cut.cpp`), qui garantit une vraie
séparation topologique — une coupe infiniment fine laisserait parfois deux
morceaux qui se retouchent en un point, toujours une seule composante
connexe aux yeux d'un traitement ultérieur.

Renvoie un morceau par composante connexe résultante : 1 seul morceau (la
région quasi inchangée) si la ligne ne traverse pas réellement la région ou
est dégénérée (`a == b`) — à l'appelant de vérifier `size() >= 2` pour savoir
si la coupe a réellement séparé quelque chose. Testé
(`tests/unit/geometry/test_cut.cpp`, 6 cas / 29 assertions) : séparation
d'un carré, coupe du pont d'un haltère (cas de jonction Y/T), ligne hors
région sans effet, points confondus sans effet, déterminisme, largeur de
bande bornée.

### Outil desktop (`Tool::DrawSatinCutLine`)

Réutilise le mécanisme presser-glisser-relâcher déjà en place pour l'outil
Bézier (`CanvasView::bezierPointDraggingMm`/`bezierPointCommittedMm`) — un
second couple de connexions sur les mêmes signaux, sans aucune modification
de `CanvasView` (les gestionnaires Bézier existants font déjà un retour
anticipé hors de l'outil `DrawBezier`, donc no-op automatique quand l'outil
actif est `DrawSatinCutLine`).

Séquence (`apps/desktop/main_window.cpp`,
`createSatinObjectWithCutLine`) :

1. Requiert une forme vectorielle sélectionnée (`selectedObject_`) ; sinon,
   message dans la barre de statut plutôt qu'une boîte de dialogue bloquante.
2. Glisser trop court (< 200 µm, clic net) ignoré silencieusement — geste
   probablement involontaire.
3. `geometry::cut_path_set` sur `source->paths.front()` ; `< 2` morceaux ->
   avertissement explicite ("tracez la ligne bien d'un bord à l'autre").
4. Boîte de dialogue densité/compensation/sous-couche, identique à celle de
   `createSatinObject()` (création automatique) pour cohérence d'expérience.
5. Chaque morceau est passé indépendamment à `auto_satin::build_satin_columns`
   (mode `Parametric`) : un morceau qui ne produit aucune colonne (trop
   petit/dégénéré) est simplement ignoré plutôt que d'annuler toute
   l'opération — la coupe a pu réussir pour la jonction visée même si un
   fragment marginal ne l'est pas.
6. Avertissement de largeur excessive identique à §*Colonne trop large*.
7. Un seul `commands::AddObjectBatchCommand` ("Colonne satin (ligne de
   coupe)"), annulable en un geste comme toutes les créations de forme de
   cette fenêtre.

Un seul geste = une seule coupe (portée v1, pas d'accumulation
multi-coupes) : succès -> retour automatique sur l'outil Sélection pour
enchaîner sur la retouche du résultat ; échec -> reste sur l'outil pour
retracer.

Testé (`tests/unit/desktop/test_main_window.cpp`,
`satinCutLineToolSplitsSelectedShapeIntoTwoSatinColumns`) : glisser souris
réel à travers un rectangle allongé sélectionné -> deux objets satin
indépendants, undo/redo, retour automatique sur Sélection.

## Objets satin paramétriques (mode `Parametric`, refonte 2026-08-04)

*État : Présent, activable par `SatinColumnsParameters::geometry_mode` (CLI :
`--satin-geometry=parametric`) · Mode par défaut toujours `Legacy` · Testé
numériquement (44 cas legacy inchangés + 6 nouveaux cas parametric) · Validé
visuellement (SVG) sur `rectangle`, `capsule`, `y`, `t`, `cross`, `trident`.*

**Défaut architectural du pipeline `Legacy`.** `build_column` convertit
chaque station dense (une tous les `station_spacing` = 500 µm) directement en
un nœud de rail `Corner` (`rail_path`) et un barreau (`SatinRung`) — alors
que `geometry::PathNode` porte déjà `tan_in`/`tan_out` (Bézier, cf.
`polyline.hpp::flatten`, De Casteljau adaptatif) et que
`stitch_generation::fill_satin_columns` implémente déjà l'essentiel de
l'étape « génération par longueur d'arc entre guides » (`ladder_correspondence`
et `resample_by_medial_spacing`, § section suivante). Le vrai problème n'était
donc pas une architecture Bézier manquante, mais que **rien ne l'utilisait** :
même en y injectant des `tan_in`/`tan_out`, `to_points` (dans `satin.cpp`)
lisait `node.pos` brut sans jamais appeler `flatten` — corrigé (§ plus bas).
Les jonctions (`StableBranchEnd`/`JunctionSeparator`/secteurs/`JunctionCore`)
opéraient sur ces nœuds denses avec une contrainte de partition exacte —
fragile par construction : c'est cette contrainte de partition, pas le
nombre de nœuds en soi, qui produisait la parade de défauts historiques
(ancre centrale, diagonale, éventail, noyau énorme, bridges égarés).

**Principe du mode `Parametric`** : au lieu d'une polyligne dense, un objet
satin devient `ParametricSatinObject` — deux rails Bézier ÉPARS
(`geometry::Path`, quelques `PathNode` à tangentes) couplés par un petit
nombre de `SatinControlPair`/`SatinAngleGuide`. Les points de couture ne
font PAS partie de cette représentation : ils sont dérivés à la demande par
`fill_satin_columns`, qui aplatit `rail_a`/`rail_b` avant de les parcourir.
La couche d'analyse dense (`compute_column_stations`, extraite de l'ancien
`build_column` sans changement de comportement) reste PARTAGÉE par les deux
modes — seule la finalisation diffère.

1. **Sélection des paires structurantes** (`select_structural_indices`) :
   union de critères déterministes — les deux extrémités, les extrema de
   largeur (`width_extrema_indices`), les sauts de largeur ADJACENTS
   (`adjacent_width_jump_indices` — cf. bug ci-dessous), les maxima de
   courbure (`curvature_maxima_indices`), et une simplification
   Douglas-Peucker de CHAQUE rail en réutilisant `geometry::simplify`
   (indices retrouvés par correspondance exacte de position, jamais une
   réimplémentation de l'algorithme). Fusion des indices trop proches
   (`minimum_control_pair_spacing`).
2. **Ajustement Bézier conjoint** (`fit_both_rails`/`fit_both_rails_recursive`) :
   un seul ensemble d'ancrages PARTAGÉ par les deux rails (jamais deux
   subdivisions indépendantes qui dériveraient l'une de l'autre — § étape 6,
   chaque paire structurante correspond exactement à une paire de points sur
   les deux rails). Par intervalle, `fit_cubic_segment` résout un cubique
   simple (Schneider, moindres carrés à tangentes fixées, sans
   reparamétrisation itérative) ; subdivision ADAPTATIVE (station la plus
   déviante, ou milieu si dépassement pur d'espacement) tant que l'erreur
   (distance OU tangente) ou l'espacement dépasse les seuils configurés,
   bornée par `minimum_control_pair_spacing` et une profondeur maximale.
3. **Lignes d'angle** (`SatinAngleGuide`) : une par paire structurante, avec
   son abscisse curviligne sur chaque rail APLATI (pas un paramètre Bézier
   brut) — consommées telles quelles par `fill_satin_columns`.

**Deux bugs de fit trouvés en généralisant sur les six formes cibles, tous
deux avec un symptôme identique (rail sortant de la région) mais des causes
distinctes :**

1. **Longueur de poignée = corde brute, pas sa PROJECTION sur la tangente.**
   Sur un bout OUVERT à plat (`extend_tip`, plancher `tip_min_width`), la
   station de fermeture saute presque uniquement en LARGEUR sur une distance
   d'axe minime — sa tangente fixée (avant/horizontale) est alors quasi
   PERPENDICULAIRE à la corde réelle. `fallbackP1 = p0 + tangente ×
   (corde/3)` utilisait la longueur de corde totale (dominée par le saut
   perpendiculaire) comme échelle de poignée le long d'une direction
   quasi-orthogonale — la poignée partait très loin dans le mauvais axe avant
   de devoir rebrousser chemin, débordant largement hors la forme (démontré :
   rectangle, poignée à x=40775 pour un bord à x=39995, soit +780 µm de
   débordement). Corrigé : la longueur d'échelle des poignées est la
   PROJECTION de la corde sur la tangente (`scale0`/`scale1` =
   `|dot(p3-p0, tangente)|`), jamais la corde brute — sur un coin quasi
   perpendiculaire, la projection est naturellement courte, produisant une
   poignée courte proche d'un coin SANS code spécial pour détecter « est-ce
   une pointe » (§ étape 5 : « discontinuité de tangente sur un coin
   structurel » — la géométrie seule tranche).
2. **`width_extrema_indices` ne détecte que les pics/creux LOCAUX, pas un
   SAUT suivi d'un PALIER.** Une station de fermeture (largeur plancher)
   collée à la première vraie station de corps (largeur pleine) n'est ni un
   maximum ni un minimum local (elle est suivie d'un PALIER à largeur pleine,
   pas d'une redescente) — ce schéma passait inaperçu, laissant un seul
   intervalle couvrir un saut de largeur de 100×, avec la même conséquence
   que le bug précédent. Ajouté `adjacent_width_jump_indices` : détecte tout
   saut relatif entre deux stations ADJACENTES (pas seulement les extrema),
   complémentaire et nécessaire.

**Robustesse de la subdivision adaptative** : le point de coupure « idéal »
(pire erreur, ou milieu) peut violer `minimum_control_pair_spacing` — typiquement
collé au tout premier/dernier point d'un intervalle contenant un saut de
largeur en bordure. Plutôt que d'abandonner toute subdivision (et garder un
segment unique très imprécis, potentiellement débordant), le point valide le
plus proche de la coupure idéale est recherché.

**Validation** (`validate_parametric_object`, § étape 12) : échantillonnage
dense des rails APLATIS (jamais les seuls nœuds de contrôle) — région
(tolérance dérivée de `bezier_fit_tolerance` : un rail trace le contour PAR
CONSTRUCTION donc est légitimement sur le bord, `in_region` seul est ambigu
pile sur une arête discrétisée), auto-intersection, croisement entre les
deux rails, lignes d'angle non dégénérées et non croisées, correspondance
monotone. Un objet refusé n'écarte QUE cette branche (`warnings`), jamais la
génération entière.

### Jonctions : recouvrement local, sans ancre (remplace StableBranchEnd/JunctionSeparator/secteurs/JunctionCore en mode `Parametric`)

Une branche = un `ParametricSatinObject` INDÉPENDANT (déjà vrai
structurellement : `build_satin_columns` construit une colonne par arête de
squelette). Aucune ancre centrale commune, aucun sommet de bissectrice,
aucun déplacement de nœud terminal, aucune diagonale, aucune rampe, aucun
noyau résiduel calculé par soustraction globale — l'apparat entier
`StableBranchEnd`/`JunctionSeparator`/secteurs/`JunctionCore` (toujours
présent et actif en mode `Legacy`) est simplement absent du chemin
`Parametric`.

À la place : après amputation de la queue instable
(`trim_unstable_junction_tail`, inchangée), `extend_into_confluence`
(variante de `extend_tip` sans critère de convergence — la largeur PEUT
légitimement croître en entrant dans le bourrelet, c'est le recouvrement
voulu) ré-échantillonne quelques sections transversales réelles au-delà du
dernier point stable, jusqu'à une distance bornée
(`junction_overlap_target` = fraction `junction_overlap_ratio` de la largeur
locale, bornée par `junction_overlap_min`/`junction_overlap_max`). S'arrête
dès que `cross_section` échoue (bord réel atteint, ou — cas `t`/`cross`,
voir plus bas — section perpendiculaire qui balaie la branche voisine).

**Cas `t`/`cross` (branches perpendiculaires de même largeur) : recouvrement
mesuré = 0, et c'est correct.** Rien n'est amputé par
`trim_unstable_junction_tail` (aucun bourrelet à détecter sur une confluence
symétrique à largeur constante), donc chaque rail atteint déjà le bord de la
confluence par son propre corps — la couverture visuelle de la confluence
est déjà complète sans extension. Y prolonger échouerait de toute façon dès
le premier pas : une section transversale prise DANS le carré de confluence,
perpendiculaire à cette branche, balaie alors la longueur de la branche
perpendiculaire et dépasse `max_satin_width` immédiatement. **Cas
`y`/`trident`** (branches à angle, largeurs différentes) : une queue
instable est réellement amputée, et le recouvrement mesuré est non nul
(0,3 à 2,5 mm selon la largeur locale).

**Ordre de couture** (`SatinJunctionPlan`, `plan_junction_stitch_order`) :
heuristique déterministe — branche la plus large en premier (cousue
dessous), branches plus fines ensuite par largeur décroissante (cousues
dessus, masquent les transitions des précédentes). Ne modifie aucune
géométrie, ordonne seulement les indices déjà construits.

**Résultat mesuré sur `trident`** (critère explicite : 3 objets, un par
branche, aucune diagonale, aucun rail traversant la confluence, aucun
éventail, recouvrements locaux visibles et bornés, déterministe) : 3 objets
indépendants (17/27/48 stations denses → 5/3/10 paires structurantes),
aucun croisement de rail entre branches (vérifié sur les rails APLATIS, pas
les seuls nœuds de contrôle), recouvrement 0,3–1 mm sur les deux branches
fines, ordre de couture `[large, moyenne, fine]`. Inspection visuelle
(`trident-parametric.svg`) : rails visuellement lisses (courbure Bézier, pas
une succession de petits segments), zone de recouvrement visible en jaune
translucide, aucune grande diagonale, aucun grand trou central.

**Branché côté `apps/desktop` ET `autodigitize.cpp` (audit satin,
2026-08-10, suite).** Les trois chemins de création satin manuelle
(`autoConvertToSatin`, `createSatinObject`, `setStitchType`) ET la
numérisation automatique (`auto_digitize`, quand `AutoOptions::use_auto_satin`
est actif — le défaut) demandent désormais `geometry_mode = Parametric` et
lisent `SatinColumnsResult::parametric_columns` en priorité — repli
automatique sur `columns` (mode `Legacy`) géré À L'INTÉRIEUR de
`build_satin_columns` lui-même (anneaux, cas refusés), donc aucun cas ne perd
de couverture. Un seul point de conversion vers `document::SatinParams`
(`autodigitize::satin_params_from_column`, template sur
`SatinColumnGeometry`/`ParametricSatinObject` — mêmes noms de champs,
exporté depuis `libs/autodigitize` et réutilisé tel quel par
`apps/desktop`) évite de dupliquer la logique entre les deux modes ET entre
les deux appelants. Les commandes d'édition de rail satin existantes
(`MoveSatinRailNodeCommand`, etc.) opèrent sans modification sur les rails
`Parametric` : ce sont de simples `geometry::Path`, seulement plus épars.

**Limites connues restantes** : les anneaux (`region.holes.size()==1`)
restent exclusivement `Legacy` même en mode `Parametric` demandé
(`parametric_columns` vide, `columns` peuplé comme avant) ; aucune
intégration éditeur DÉDIÉE (déplacer une `SatinControlPair` comme entité
propre, verrouiller une `SatinAngleGuide` indépendamment du nœud de rail
qu'elle partage aujourd'hui) n'est implémentée — seule l'édition de nœud de
rail générique est disponible, ce qui couvre l'usage courant mais pas la
manipulation fine des paires structurantes en tant que telles.

## Génération par barreaux (`fill_satin_columns`, Lot 2)

*État : Présent · Testé numériquement · Validé visuellement (SVG) · non validé
simulateur/physique.*

Quand un satin porte des **barreaux** (`SatinParams.rungs` ≥ 2), la génération
utilise `fill_satin_columns` au lieu de `fill_satin` :

- **Correspondance par sections** : chaque paire de barreaux consécutifs découpe
  les rails en intervalles correspondants, appariés par correspondance locale
  **ladder** (voir *Correction de l'appariement* plus haut — rails de
  longueurs, nombres de nœuds et courbures différents autorisés, sans
  éventail). Les barreaux sont **traversés exactement**.
- **Espacement perpendiculaire** : le pas n'est plus mesuré le long d'un rail
  mais sur la **ligne médiane** (≈ perpendiculaire aux fils) — on ré-échantillonne
  la médiane de chaque intervalle par la densité demandée.
- **Séquence** L0, R0, L1, R1, … déterministe (chaque point cousu traverse la
  colonne). Un satin **sans barreaux** (manuel/legacy) retombe sur `fill_satin`.

Vérifié : espacement médian régulier, barreaux exacts, rails de longueurs
différentes, déterminisme (`tests/unit/stitch/test_satin.cpp`) ; zigzag superposé
dans les SVG `tests/golden/auto-satin/`.

### Saut plutôt qu'un point continu disproportionné entre deux barreaux non-ruban (audit lettres, 2026-08-12)

*État : Présent (`SatinResult::jump_before`) · Testé numériquement (géométrie
synthétique + géométrie réelle) · non validé simulateur/physique.*

**Origine** : retour utilisateur sur un projet réel (sceau "GISTRE", texte en
périphérie) — le satin automatique produisait un résultat visuellement
incohérent sur les lettres, en particulier au coude intérieur d'une lettre en
T. Export debug de l'objet en cause (`Satin region 301 - section 1/4`) : deux
barreaux consécutifs, `rung0` quasi ponctuel (301 µm, une pointe de section)
immédiatement suivi de `rung1` à 5431 µm — rail A avance presque
horizontalement entre les deux, rail B presque verticalement (~88° d'écart).
`fill_satin_columns` interpolait quand même un ruban continu sur cet
intervalle : la ré-échantillonnage par ligne médiane
(`resample_by_medial_spacing`) atteint son abscisse cible sur un rail AVANT
l'autre quand les deux rails divergent autant, produisant un avant-dernier fil
dont un côté est déjà au barreau cible et l'autre non — un point continu
d'environ 5 mm, puis le barreau exact lui-même à la même largeur : une
diagonale visible qui traverse la lettre, suivie d'un aller-retour quasi
identique.

**Piste écartée** : un seuil de largeur absolue (barreau > X mm ⇒ saut). Les
sections de lettre touchant une jonction ont légitimement des barreaux larges
(5 à 6 mm, cf. *Jonctions : recouvrement local* plus haut —
`extend_into_confluence` fait CROÎTRE la largeur en entrant dans le
recouvrement, par conception) : un seuil absolu aurait déclenché un saut sur
un ruban parfaitement valide, juste large.

**Correctif** — `non_ribbon_interval` (`libs/stitch_generation/src/satin.cpp`) :
compare la DIRECTION d'avance de chaque rail entre deux barreaux consécutifs
(`a1.pa - a0.pa` vs `a1.pb - a0.pb`), pas leur largeur. Au-delà de ~75°
d'écart (et seulement si les deux avances dépassent 0,8 mm — sous ce plancher,
le bruit sous-résolution ne justifie aucun saut), la colonne ne se comporte
plus comme un ruban sur cet intervalle : aucun fil intermédiaire n'est
interpolé, le barreau suivant devient un point de reprise après un SAUT
(aiguille levée) plutôt qu'un point cousu. Une terminaison effilée ORDINAIRE
(largeur qui décroît jusqu'à un point, rails toujours co-directionnels) garde
des directions colinéaires et ne déclenche donc jamais de saut — seul un
changement de direction franc le fait, jamais une simple variation de
largeur. Pratique standard en broderie (logiciels commerciaux du métier :
lever le fil plutôt que forcer un point disproportionné) plutôt qu'une
invention propre au projet.

`SatinResult::jump_before` (indices dans `satin`) porte l'information jusqu'à
`stitch_generation::generate.cpp`, qui émet un `Jump` (aiguille levée) au lieu
d'enchaîner un `Stitch` à ces indices (`emit_polyline_with_breaks`, distincte
d'`emit_polyline` pour ne pas perturber les passes sans saut — sous-couches,
locks). L'orientation entrée/sortie (§ *Entrée/sortie et points de fixation*)
inverse `result.satin` : les indices de `jump_before` sont recalculés
(`idx_after = taille - idx_avant`, l'arête cassée reste la même arête
physique, cf. commentaire dans `generate.cpp`) puis retriés.

**Vérifié** : `tests/unit/stitch/test_satin.cpp` (coin quasi perpendiculaire →
saut, garde-fou anti-faux-positif sur une terminaison effilée ordinaire) ;
`tests/unit/auto_satin/test_columns.cpp`, géométrie EXACTE de la lettre en T
en cause (contour à 37 sommets) — invariant testé : aucun point cousu ne
dépasse la largeur du plus large barreau RÉEL qui l'encadre (pas "aucun point
large", qui serait faux pour une jonction légitimement large) ; le mécanisme
de saut s'engage bien sur cette géométrie réelle (2 des 4 sections).

### Seuil angle/distance plutôt qu'angle brut (deuxième cas réel, 2026-08-13)

*État : Présent (`kJumpDegPerMm`) · Testé numériquement (deux cas réels
distincts + garde-fous synthétiques) · non validé simulateur/physique.*

**Défaut trouvé en usage réel** : un DEUXIÈME export debug (même sceau,
`Satin region 267 - section 1/6`) montrait le même artefact — un point
continu d'environ 3,3 mm faisant l'aller-retour — sur un coude à seulement
**~68,5°**, sous le seuil de 75° calibré sur le premier cas (~88°). Baisser
le seuil brut à 50° corrigeait ce deuxième cas mais **régressait** le test
« virage » existant (Lot 3, § *Sous-couches et compensation*) : un virage
LÉGITIME (rail intérieur droit et court, rail extérieur en long détour)
présente un angle bout-à-bout de **~55°** — à peine 13° sous le vrai défaut à
68,5°. La fenêtre de seuil brut sûre entre ces deux cas était trop étroite
(quelques degrés) pour être fiable face à un futur troisième cas.

**Correctif** — l'angle brut est remplacé par un rapport **angle/distance
parcourue** (`kJumpDegPerMm = 10°/mm`) : ce qui distingue réellement un
virage légitime d'un défaut n'est pas l'angle en lui-même, mais la BRUTALITÉ
du changement de direction. Le virage légitime étale son virage sur ~14 mm
(≈4°/mm) ; les deux défauts réels observés concentrent un angle comparable
sur seulement 3,7 mm (≈24°/mm) et 1,5 mm (≈45°/mm) — plus d'un ordre de
grandeur d'écart entre le cas légitime le plus proche et le défaut le plus
proche, une marge bien plus sûre qu'un seuil d'angle brut ne pouvait
l'offrir. `kJumpMinSpan` (0,8 mm) inchangé.

**Vérifié** : nouveau cas dans `tests/unit/stitch/test_satin.cpp`, géométrie
EXACTE des rails/barreaux du deuxième export debug (7 nœuds Bézier épars par
rail) ; suite complète de `test_satin.cpp`/`test_satin_pairing_metrics.cpp`
sans régression, y compris le test « virage » qui avait révélé la limite du
seuil brut.

**Précision a posteriori** (audit complémentaire, même jour) : les largeurs
proches de `max_satin_width` observées sur 2 des 4 sections dès leur première
station ne sont PAS un artefact de recouvrement de jonction (`extend_into_confluence`)
comme d'abord supposé — vérifié en dumpant le graphe de squelette brut de
cette lettre (`SkeletonGraph`, 5 arêtes/6 nœuds) : ces deux sections sont deux
branches INDÉPENDANTES (aucune jonction), et leur largeur de départ élevée
reflète simplement la largeur réelle de l'empattement à cet endroit. Le
squelette complet a en réalité 5 arêtes ; la 5e (~28 mm, reliant le crochet
latéral gauche à la jonction du bas) est REJETÉE dans son intégralité :
`compute_column_stations` y mesure une largeur locale >6 mm sur la totalité
de ses ~54 stations échantillonnées (vérifié : ~8,1 mm à mi-parcours, calcul
indépendant par intersection de segments) — un empattement réellement trop
large pour du satin à cet endroit, pas un bug de mesure. Voir *Avertissements
auto-satin remontés à l'utilisateur* ci-dessous pour la suite donnée à cette
branche rejetée.

### Repli tatami sur une branche auto-satin rejetée, jamais de zone sans point (2026-08-13)

*État : Présent (`geometry::subtract_polygons`, `AutoResult::warnings`) ·
Testé numériquement (chaîne complète, géométrie réelle rasterisée,
vérification de couverture géométrique de bout en bout) · non validé
simulateur/physique.*

**Défaut trouvé en usage réel** (suite directe de l'audit ci-dessus) : quand
`build_satin_columns` rejette une branche de squelette (ex. trop large,
comme la 5e arête de la lettre en T ci-dessus), le rejet est bien
diagnostiqué (`SatinColumnsResult::warnings`) mais `autodigitize.cpp` ne
lisait jamais ce champ. Tant qu'au moins UNE branche de la même région
réussit (`sectionCount > 0`), la numérisation automatique se poursuit
normalement, satisfaite d'avoir produit *quelque chose* — la portion couverte
par la branche rejetée, elle, ne reçoit RIEN (ni satin, ni tatami, ni
contour), sans le moindre signal dans l'interface. Sur la lettre en T de
l'audit : ~28 mm de squelette disparaissaient silencieusement à chaque
numérisation automatique de ce logo.

**Première itération (incomplète)** : remonter `SatinColumnsResult::warnings`
jusqu'à une boîte de dialogue utilisateur (`AutoResult::warnings`), sans
générer de point de repli. Retour utilisateur explicite : *"il faut
absolument que ça couvre toute la zone"* — un avertissement, aussi visible
soit-il, laisse toujours un trou physique dans le motif cousu. Étendu en
repli tatami automatique.

**Primitive géométrique** — `geometry::subtract_polygons(base, cutouts)`
(nouveau, `libs/geometry`) : différence booléenne Clipper2 (règle
**NonZero**, pas EvenOdd comme `clean.cpp`/`cut.cpp`) entre `base` et l'union
de `cutouts`. NonZero choisi délibérément : des bandes de colonnes satin
adjacentes qui se chevauchent légèrement (même barreau partagé) s'annuleraient
localement sous EvenOdd, laissant un trou parasite au milieu d'une zone
pourtant couverte. Comme NonZero est sensible à l'orientation (contrairement
à EvenOdd), `base.outer`/`base.holes`/chaque `cutout` sont renormalisés en
interne (CCW/CW) avant l'opération — l'orientation d'entrée n'est PAS
garantie ailleurs dans `geometry/`, qui utilise EvenOdd précisément pour s'en
affranchir.

**Détection du besoin de repli** — signal STRUCTUREL, jamais une
correspondance de texte sur `warnings` : `network.debug.graph.edges.size() >
sectionCount` (le squelette élagué compte plus d'arêtes que de sections
produites). Une correspondance sur le texte des avertissements a été tentée
puis abandonnée : `warnings` porte aussi des messages purement informatifs
sans rapport avec une couverture manquante (ex. *"couture fermée : routage
cyclique de quatre sections"* sur un anneau déjà parfaitement couvert), et un
simple calcul d'aire du reliquat géométrique SANS ce garde-fou déclenchait à
tort sur des réseaux satin entièrement réussis : un rail satin (surtout en
mode Parametric, ajusté en Bézier épars) approxime toujours la forme source
avec une légère tolérance même quand tout réussit, produisant plusieurs mm²
de reliquat "naturel" aux pointes/jonctions — repérage régressé sur les
fixtures existantes ("réseau en T", "anneau fin") avant d'ajouter ce
garde-fou structurel.

**Recouvrement délibéré, pas une couture pile au bord** — `shrink_strips_for_cutout`
rétrécit chaque bande satin de 0,4 mm avant de la soustraire de la région :
le territoire de repli déborde ainsi À L'INTÉRIEUR de la bande satin plutôt
que de s'arrêter exactement à son bord. Défaut trouvé par revue lors du
premier essai (grandir la bande AVANT soustraction plutôt que la rétrécir) :
l'effet est inversé — le repli recule et laisse un interstice, pas un
recouvrement. Un recouvrement (léger double-point) est anodin ; un
interstice ne l'est pas. Même principe que le recouvrement de jonction
(`extend_into_confluence` ci-dessus) et la pratique standard du métier
(chevaucher plutôt que raccorder pile, cf. audit Wilcom Hatch — Column
B/miter joints : *"les trous ne sont pas des échecs, c'est de la physique
attendue ; il faut faire disparaître le trou par le recouvrement, pas par la
géométrie seule"*).

**Mise en œuvre** (`libs/autodigitize/src/autodigitize.cpp`) : pour chaque
section satin réussie, une bande approximative (rail A aller + rail B
retour, rails aplatis) ; la région source moins l'union rétrécie de ces
bandes donne le(s) reliquat(s) ; chaque reliquat ≥ 0,5 mm² (seuil
délibérément bas et INDÉPENDANT de `min_fill_area_mm2` — ce dernier répond à
"cette région entière vaut-elle un remplissage ?", pas à "ce reliquat déjà
largement couvert mérite-t-il d'être comblé ?") devient un nouvel objet
vectoriel + un objet de broderie tatami, nommés explicitement ("zone non
couverte par le satin" / "Remplissage repli région"). `AutoResult::warnings`
reste rempli (préfixé par la région source) : les avertissements expliquent
maintenant POURQUOI une zone est en tatami plutôt qu'en satin comme le reste
de la région, la boîte de dialogue desktop (`autoDigitize()`/`segmentWithAi()`)
étant reformulée en conséquence.

**Vérifié** : `tests/unit/geometry/test_boolean.cpp` (7 cas : trou net,
recouvrement toute la région, deux cutouts qui se chevauchent avec des
orientations OPPOSÉES sans trou parasite, déterminisme) ;
`tests/unit/autodigitize/test_autodigitize.cpp` — géométrie EXACTE de la
lettre en T (37 sommets, la même que l'audit ci-dessus), **rasterisée** et
passée par la VRAIE chaîne segmentation → vectorisation → auto-satin (pas
`build_satin_columns` en isolation) : reconstruction indépendante de la
couverture (bandes satin des sections réussies + zones de repli tatami) et
vérification géométrique que la région source moins cette couverture ne
laisse aucun reliquat significatif ; suite complète (506 cas) sans
régression, y compris les fixtures "réseau en T" et "anneau fin" qui
auraient (à tort) déclenché un repli sans le garde-fou structurel.

### Barreaux par défaut pour un satin manuel (`default_rungs`)

*État : Présent · Testé numériquement.* Correctif de revue.

**Défaut trouvé par revue** : un satin créé **manuellement** (deux rails
seuls, `MainWindow::createSatinObject` via **Broderie ▸ Colonne satin…**, cf.
`rails_from_contour`) n'avait jamais de barreau, et retombait donc sur
`fill_satin` — qui n'implémente qu'un **sous-ensemble** de `SatinConfig`
(densité, compensation pull symétrique, sous-couche centrale). Les autres
réglages Lot 3/4 (terminaisons, split, sous-couches de bord/zigzag,
compensation push/pull asymétrique) restaient **silencieusement sans effet**,
alors que l'inspecteur les expose et les laisse éditer sans aucune
distinction ni avertissement pour ce type de satin : un utilisateur pouvait
activer « Terminaison : Effilée » sur un satin manuel, voir le champ persister
dans le `.osp`, sans jamais observer le moindre changement dans le résultat
cousu.

**Correction** : `default_rungs(rail_a, rail_b, spacing)` (nouvelle fonction
publique de `satin.hpp`) génère des barreaux par défaut en réutilisant **la
même correspondance ladder** que `fill_satin` (aucun nouvel algorithme de
correspondance, aucun risque de régression géométrique), à l'espacement
`density`. `createSatinObject` les assigne à `SatinParams.rungs`, ce qui fait
désormais passer **tout nouveau satin manuel** par `fill_satin_columns` — le
seul chemin implémentant l'intégralité de `SatinConfig`. Portée volontairement
restreinte à ce seul point d'entrée : un projet historique chargé depuis un
`.osp` sans barreaux continue de suivre `fill_satin` sans changement de
comportement (rétrocompatibilité), et `fill_satin` lui-même n'est pas modifié.

Vérifié : `default_rungs` produit au moins deux barreaux sur une colonne
simple (vide sur des rails dégénérés) ; avec `cap_end = Tapered`, le même
rail/config produit un bout à pleine largeur via `fill_satin` (réglage
ignoré, ancien comportement) contre un bout réellement effilé via
`fill_satin_columns` + `default_rungs` (réglage appliqué) — démontre que le
défaut est bien corrigé, pas seulement que des barreaux existent.

### Édition interactive de plusieurs guides

Sélectionner une colonne satin puis activer **Broderie ▸ Éditer les guides
satin…** (raccourci `G`) affiche chaque barreau et deux poignées. Une extrémité
peut être glissée indépendamment : elle est projetée exactement sur son rail et
la colonne est régénérée. Plusieurs guides successifs imposent donc plusieurs
orientations locales ; `fill_satin_columns` interpole la correspondance de
façon monotone entre chaque paire de guides.

Un geste est refusé s'il ferait franchir deux guides sur un seul rail ou les
rapprocherait de moins d'un demi-pas de densité. Cette validation partage
l'invariant du générateur, ce qui évite qu'un guide visible soit ensuite ignoré
silencieusement. Chaque glisser produit une seule commande annulable
**Déplacer un guide satin**. Les guides restent les `SatinParams.rungs`
historiques : la persistance `.osp` demeure rétrocompatible.

Un clic sur le segment d'un guide le sélectionne et le met en évidence. La
commande **Ajouter un guide satin** (`Maj+G`) partage automatiquement le plus
grand intervalle disponible sur les deux rails ; elle est refusée si le nouveau
guide serait trop proche de ses voisins. **Supprimer le guide satin
sélectionné** conserve toujours au moins deux guides, seuil nécessaire à une
colonne paramétrique. Ajout, suppression et déplacement utilisent trois
commandes distinctes et sont entièrement annulables/rétablissables. L'insertion
préserve aussi l'ordre de progression croissant ou décroissant des guides, afin
que les extrémités employées par le routage textile restent exactes.

Le guide terminal qui porte une **jonction de réseau** est structurel : il reste
sélectionnable et explicitement marqué comme verrouillé, mais ses poignées et
l'action de suppression sont désactivées. Le générateur ne couvre que
l'intervalle compris entre ses guides extrêmes ; déplacer ou supprimer ce guide
pourrait donc raccourcir une section et créer un vide ou un repli à la jonction.
Quand ce guide verrouillé est sélectionné, **Ajouter un guide satin** crée un
guide interne dans l'intervalle directement adjacent de chacune des sections
incidentes — jamais dans un grand vide éloigné de la jonction. L'opération est
globale : si le réseau est incomplet, une section dupliquée ou un intervalle
inadmissible, aucun guide n'est ajouté. Une seule commande undo/redo couvre
toutes les sections. Chaque guide interne reste ensuite déplaçable section par
section ; ce mécanisme amorce l'orientation de toutes les branches sans déplacer
la jonction structurelle. Celle-ci est reconnue par l'ordre géométrique des
stations projetées sur les deux rails, et non par son index de stockage, y
compris après un import aux barreaux inversés ou désordonnés.

Chaque groupe de guides internes créés ensemble par un ajout coordonné à une
jonction porte un `link_id` (`SatinRung.link_id`, optionnel) — un identifiant
local au réseau (`source_vector`), alloué de façon déterministe et monotone par
`next_satin_guide_link_id` (échoue explicitement si les identifiants uint32 du
réseau sont épuisés plutôt que d'en réémettre un déjà utilisé). Un guide ajouté
indépendamment n'a pas de `link_id` ; les projets historiques n'en ont jamais.
Persisté dans le `.osp` (`rungs[].linkId`, validation stricte : entier `uint32`
exact, aucune troncature silencieuse d'une valeur négative/flottante/hors
bornes).

**Invariant central** : un groupe lié est identifié par la paire
`(source_vector, link_id)` — jamais par le seul `link_id`, qui n'est pas
globalement unique entre réseaux distincts. `satin_linked_guides` énumère les
membres d'un groupe (un guide par section touchée par la jonction d'origine,
jamais plus), rejette la totalité si une section porte deux guides du même
`link_id` (donnée corrompue), si une section déclarée manque, si les membres ne
touchent pas la même jonction ou si leur progression n'est pas strictement
monotone sur les deux rails, et
trie le résultat par `(section_index, embroidery_id, guide_index)` — mutation
et énumération restent donc déterministes indépendamment de l'ordre des objets
dans le document, et un même identifiant numérique réutilisé dans un autre
réseau (`source_vector` différent) n'est jamais confondu avec le groupe.

**Suppression de groupe atomique.** Supprimer un guide lié (`Broderie ▸
Supprimer le guide satin sélectionné`) supprime désormais l'identité logique
entière : tous les guides du groupe, dans toutes leurs sections, en une seule
commande **Supprimer des guides satin coordonnés** (`RemoveSatinGuidesCommand`)
annulable/rétablissable comme un tout. Refusée en bloc — aucune section n'est
touchée — si une section tomberait à moins de deux guides restants.

**Déplacement de groupe atomique — geste explicite.** Un glisser ordinaire
d'une extrémité de guide (sans modificateur) reste **local** : il ne modifie
que l'angle de cette section, comme avant (édition d'angle intrinsèquement
locale — deux sections d'un même réseau peuvent légitimement avoir des largeurs
et orientations différentes). Pour déplacer tout le groupe de façon cohérente,
l'utilisateur maintient **Maj** en glissant une extrémité d'un guide lié :
`move_satin_guide_group` calcule, pour la section glissée, sa position
normalisée `t` dans l'intervalle `[station de la jonction, station du voisin
suivant]` (sur chaque rail), en déduit un delta normalisé unique à partir du
point relâché, puis applique **ce même delta** à `t` dans **chaque** section du
groupe — recalculée entièrement à partir de la géométrie propre de cette
section (ses propres rails, sa propre densité). Aucune coordonnée ni angle brut
n'est jamais recopié d'une section à l'autre ; seul le delta normalisé
traverse la frontière. Le résultat est appliqué par une seule commande **Déplacer
des guides satin coordonnés** (réutilise `MoveSatinGuidesCommand`), tout ou
rien : si une seule section sortirait de son intervalle admissible (moins d'un
demi-pas de densité de sa marge, ou guide plus adjacent à sa jonction — topologie
modifiée entretemps), aucune section n'est mutée et un message explique le refus.
Un Maj+clic sans déplacement est un no-op exact et ne crée pas d'entrée dans
l'historique. L'infobulle du guide et le message de statut du mode documentent
ce geste.

Limite actuelle : le déplacement de groupe ne s'applique qu'aux guides internes
directement adjacents à leur jonction (ceux créés par l'ajout coordonné) — un
guide qui aurait perdu cette adjacence (un autre guide inséré entre lui et la
jonction) redevient local et refuse le geste de groupe plutôt que de deviner un
intervalle arbitraire. Le guide structurel de jonction lui-même reste
volontairement verrouillé (ni déplaçable ni supprimable) ; le déverrouiller
exigerait un calcul garantissant simultanément la couverture, la monotonie et
l'absence de repli dans toutes les sections incidentes.

## Finitions : points courts, split, terminaisons (Lot 3)

*État : Présent · Testé numériquement · Validé visuellement (SVG).* Toutes ces
options sont **désactivées par défaut** (comportement inchangé) et éditables dans
l'inspecteur (section satin) ; elles sont persistées dans le `.osp`.

- **Points courts** (`ShortStitchMode`) : dans un virage serré (le rail intérieur
  avance beaucoup moins que l'extérieur), les pénétrations intérieures sont
  *rentrées* vers l'axe (`SingleInset`, `MultiLevelInset` = motif triangulaire de
  profondeurs) pour ne pas s'entasser sur le bord, ou allégées
  (`RemoveAndRedistribute` : un fil serré sur deux, jamais un barreau, jamais deux
  d'affilée).
- **Split** (`SplitStitchMode`) : une traversée plus longue que `max_stitch_length`
  reçoit des pénétrations intermédiaires. `Simple` (colinéaire régulier),
  `Staggered` (décalage cyclique) et `DeterministicJitter` (variation
  **reproductible**, graine = identifiant de l'objet) évitent une **ligne centrale**
  visible. Jamais d'aléa non déterministe.
- **Terminaisons** (`SatinCapType`) : `Flat` (barreau final), `Rounded` (réduction
  arrondie de largeur), `Tapered` (effilé vers une pointe **sans** empiler sur une
  coordonnée unique — largeur minimale ~18 %), `Automatic`.

Vérifié : inset modifie le rail intérieur en virage, `RemoveAndRedistribute`
réduit les pénétrations, split ajoute des points décalés (staggered ≠ ligne
centrale ; jitter déterministe), taper réduit la largeur au bout sans l'annuler
(SVG `tests/golden/auto-satin/lot3-*.svg` : effilé 5,00 → 0,91 mm).

## Sous-couches et compensation (Lot 4)

*État : Présent · Testé numériquement · Validé visuellement (SVG).* Toutes
désactivées par défaut, éditables (inspecteur) et persistées (`.osp`).

**Modèle de passes** (§4) : chaque commande de la séquence porte une `StitchPass`
(`Underlay`, `TopStitch`, `Travel`, `Lock`, `Manual`) — non sérialisée (la
séquence est régénérée), qui permet d'**identifier, filtrer et analyser** les
passes séparément. Les sous-couches sont émises **avant** la couche supérieure.

- **Center walk** : point droit sur la médiane (retrait aux extrémités).
- **Edge walk** : deux chemins internes rentrés des rails (`underlay_edge_inset`).
- **Zigzag underlay** : satin léger (largeur réduite, pas plus grand).
- Ordre : **center → edge → zigzag → satin** (recommandé).

**Compensation** (§11), appliquée à la géométrie des paires avant génération :

- **Pull latérale asymétrique** : décalage par côté = base symétrique
  (`pull_compensation`) + fixe gauche/droite (`pull_left`/`pull_right`) +
  proportionnel à la largeur, borné (`pull_max`).
- **Push longitudinale** : `push_start`/`push_end` étendent (ou rétractent) la
  colonne aux extrémités le long de l'axe.

**Correctif de revue — rétraction bornée.** Contrairement à la compensation
pull (bornée par `pull_max`), `push_start`/`push_end` n'avaient **aucune
borne** : une rétraction excessive (`push` très négatif, p. ex. une valeur
saisie par erreur en mm au lieu de µm) faisait passer le premier (ou dernier)
fil de la colonne **de l'autre côté** de son voisin, inversant leur ordre le
long de l'axe — un point auto-croisé visible en tout début/fin de colonne, la
sous-couche `mids`/`cumMid` étant elle-même calculée **après** ce décalage
(§ sous-couches ci-dessus, potentiellement corrompue en cascade). Corrigé :
une rétraction ne dépasse désormais jamais la station voisine (borne dérivée,
pas un seuil arbitraire — la même logique que l'exclusion des rails à
l'ancrage des jonctions plus haut). Une extension (push positif) reste
volontairement non bornée : elle éloigne le fil sans jamais croiser un autre
point. Vérifié : une rétraction demandée de −50 mm sur une colonne de 1 mm de
densité reste proche de son point d'origine (`tests/unit/stitch/test_satin.cpp`).

Vérifié : center/edge/zigzag = 4 passes distinctes ordonnées, pull élargit **un
seul côté** (asymétrique), push étend le bout, déterminisme, aller-retour `.osp`
(SVG `tests/golden/auto-satin/lot4-*.svg` : sous-couches en vert).

**Défaut trouvé en usage réel (2026-08-13)** — sous-couche centrale à la
densité du zigzag principal au lieu de `underlay_spacing` : sur un satin
**avec barreaux** (`fill_satin_columns`, auto-satin ou satin manuel avec
barreaux par défaut — le chemin réellement emprunté par la quasi-totalité des
satins de l'application), la sous-couche centrale reprenait simplement le
milieu (`mids`) de **chaque fil** du zigzag principal, lui-même espacé à
`density` (souvent 0,4 mm) — au lieu d'être ré-échantillonnée à
`underlay_spacing` (2 mm par défaut). `underlay_spacing` existe et est
correctement utilisé par `fill_satin` (satin manuel/legacy, sans barreaux) ;
`fill_satin_columns` l'ignorait silencieusement en réutilisant `mids` tel
quel. Conséquence concrète : des points de sous-couche de 0,3-0,4 mm au lieu
de ~2 mm, systématiquement signalés `point-court` par l'analyse (risque de
casse du fil et de sur-densité) — et un nombre de points de sous-couche
inutilement élevé (5x plus que nécessaire à densité 0,4 mm). Corrigé en
ré-échantillonnant `mids`/`cumMid` (déjà calculés pour `keepEnd`) à
`underlay_spacing` via `point_at` (même primitive que le reste du fichier),
sur l'intervalle `[retract, totalMid - retract]` — bornes identiques à
l'ancien filtre par indice `keepEnd`. Vérifié :
`tests/unit/stitch/test_satin.cpp` (colonne de 20 mm, densité 0,4 mm,
`underlay_spacing` 2 mm → ~10 points de sous-couche, tous espacés de plus de
0,5 mm — pas ~50 points à 0,4 mm comme avant le correctif).

Reste (lots suivants) : tatami avancé (Lot 7). Voir
`docs/stitch-feature-gap-audit.md`.

## Entrée/sortie et points de fixation (Lot 5)

*État : Présent · Testé numériquement · Validé visuellement (SVG).* Éditables
(inspecteur) et persistés (`.osp`).

**Points d'entrée/sortie** (§12) : `entry_point` et `exit_point` (optionnels)
orientent la couture. Le satin est **retourné** si son extrémité de départ est
plus proche du point de sortie que du point d'entrée (coût = somme des distances
extrémités↔points) — jamais de réordonnancement partiel, seulement le sens. Sans
point, l'orientation issue du générateur est conservée.

**Points de fixation** (`SatinLock`, émis en passe `Lock` uniquement) : ancrent
le fil au **début** et à la **fin** de l'objet, jamais par sous-passe (un lock au
départ, un à l'arrivée). Chaque bout se règle indépendamment
(`lock_start`/`lock_end`), avec longueur (`lock_length`) et nombre de passages
(`lock_passes`) communs.

- **`None`** : aucun point de fixation.
- **`BackAndForth`** : aller-retour court le long du premier/dernier point.
- **`Triangle`** : petit triangle d'ancrage.
- **`MicroZigzag`** : micro-zigzag progressant dans l'axe de couture.

Toutes les formes sont **bornées** (`lock_length`, défaut 0,8 mm) et ancrées à
l'extrémité exacte du satin (continuité : pas de saut parasite grâce à
l'enchaînement de passes à position identique).

**Correctif de revue — borne réelle, pas seulement nominale.** `lock_length`
n'était en réalité **jamais comparée** à la distance disponible : `toward`
(le point voisin qui donne la direction du lock) est le point du rail
**opposé** — donc la largeur locale du barreau, pas un point lointain le long
de la couture. Sur une colonne fine (largeur < `lock_length`, cas courant
pour du texte fin ou un petit motif), le lock piquait **hors de la matière
déjà cousue**, dans du tissu vierge, sans qu'aucune borne ne l'en empêche.
Corrigé : la longueur effective du lock ne dépasse désormais jamais la
distance réelle vers `toward` (repli sur la longueur demandée uniquement
dans le cas dégénéré où `toward == anchor`, sans référence géométrique
fiable). Vérifié : sur une colonne de 0,3 mm de large avec `lock_length` par
défaut (0,8 mm), tous les points du lock restent dans la largeur réelle,
quel que soit le type (`tests/unit/stitch/test_satin.cpp`).

Vérifié : `None` → vide ; les autres progressent dans l'axe de couture et restent
bornés ; le point d'entrée fait démarrer la couture à la bonne extrémité ; locks
en passe `Lock`, exactement deux groupes (début/fin) ; aller-retour `.osp` des
champs Lot 5 (SVG `tests/golden/auto-satin/lot5-lock-*.svg` : fixations en rouge).

## Routage multi-colonnes (Lot 6)

*État : Présent · Testé numériquement · Validé visuellement (SVG).* Automatique
à la génération ; aucun réglage persisté.

Une forme décomposée (Y, T, croix…) devient **plusieurs colonnes satin** de même
couleur et même source. Sans routage, elles seraient cousues dans l'ordre du
document, avec de longs sauts. §13 les ordonne et les oriente ensemble
(`route_columns`) :

- **Ordre** : une jonction explicite commune et géométriquement admissible
  prime sur une proximité fortuite ; à égalité, glouton plus proche voisin
  depuis la position courante de l'aiguille, puis amélioration **2-opt**.
  Déterministe.
- **Orientation** : pour un ordre donné, le sens de chaque colonne est résolu
  **exactement** par programmation dynamique sur ses deux extrémités : maximum
  de liaisons par jonction, puis distance minimale
  (l'orientation choisie est imposée à `generate_satin` via entrée/sortie).
- **Liaisons** : une transition **justifiée par une jonction commune validée**
  tolère jusqu'à `underpath_max` (défaut 8 mm) en **trajet caché** (passe
  `Travel`, running stitch — pas de coupe) ; **sans** jonction, seul un
  quasi-contact (`underpath_max_without_junction`, défaut 1,5 mm — bien plus
  strict) l'autorise. Au-delà de la borne applicable, la liaison reste un
  **saut**. Minimise les coupes et les déplacements à découvert.

Une jonction déclarée mais séparée de plus de `underpath_max` est traitée comme
incohérente et n'influence ni l'ordre ni le type de liaison. Cette garde évite
qu'un `.osp` altéré force un trajet caché arbitraire. Limite actuelle : le trajet
entre deux sections reste un segment échantillonné ; il ne suit pas encore le
centre d'une branche déjà cousue lors d'un retour vers une jonction.

**Correctif « auto-satin béton » (suite)** : `route_columns` ne regardait la
jonction que pour l'ORDRE et l'ORIENTATION ; le TYPE de liaison (trajet caché ou
saut) ne dépendait que de la distance, sans jamais vérifier qu'elle était
justifiée par cette même jonction. Deux colonnes **sans aucun lien
topologique** mais distantes de 5 à 8 mm — deux lettres rapprochées, une forme
en C dont les deux bouts se frôlent sans être reliés — étaient donc cousues en
trajet caché à travers un espace dont rien ne garantissait qu'il était couvert
de tissu. Corrigé : le type de liaison distingue désormais explicitement les
deux cas (jonction validée vs proximité seule), chacun avec sa propre borne.
Une jonction reste géométriquement anchrée exactement (§ ancrage des jonctions
ci-dessus), donc digne de confiance jusqu'à 8 mm ; une simple proximité, sans
cette garantie, ne l'est que jusqu'à 1,5 mm.

Le groupe routé est **contigu** et limité aux colonnes auto (porteuses de
barreaux) de couleur et source identiques : l'ordre inter-groupes et le reste du
document sont préservés. Un satin manuel isolé n'est pas réordonné.

**Correctif de revue — le point de décision doit être le point réellement
cousu.** `column_endpoints` (extrémités représentatives d'une colonne pour le
routage) utilisait le milieu **brut** des barreaux d'about, alors que la
génération réelle (`fill_satin_columns`) peut **décaler** ces mêmes extrémités
via `push_start`/`push_end` (§ sous-couches et compensation, Lot 4) avant de
coudre. La décision Underpath/saut se fondait donc sur un point différent de
celui effectivement cousu : un écart évalué juste sous le seuil pouvait, une
fois le décalage réel appliqué, le dépasser (trajet caché accordé à travers un
espace non garanti couvert — précisément le défaut visé par le correctif
« auto-satin béton (suite) » ci-dessus, réintroduit par un chemin différent),
ou inversement imposer un saut évitable. Corrigé : `column_endpoints`
reproduit désormais le même décalage (et la même borne de rétraction, § Lot 4)
que celui réellement appliqué à la génération. Vérifié : deux colonnes dont
l'écart **brut** entre barreaux (900 µm, sous le seuil sans jonction de
1,5 mm) aurait autorisé un trajet caché, mais dont l'écart **réel** une fois
`push_end` (rétraction de 700 µm) appliqué dépasse ce seuil (1,6 mm),
produisent désormais un saut — pas un trajet caché injustifié
(`tests/unit/stitch/test_routing.cpp`).

Vérifié : liste vide → plan vide ; une colonne → liaison de départ, aucun saut ;
réordonnancement minimisant le déplacement ; orientation par l'extrémité proche ;
liaison longue → saut, liaison courte → trajet caché ; **liaison proche (5 mm)
SANS jonction commune → désormais un saut** (défaut corrigé) ; la même liaison
justifiée par une jonction reste un trajet caché ; un quasi-contact (1 mm) sans
jonction reste toléré ; à la génération, un groupe adjacent n'émet qu'un saut
initial (le reste cousu), un groupe éloigné **ou sans jonction commune**
conserve ses sauts (SVG `tests/golden/auto-satin/lot6-route-*.svg` : trajets
cachés en bleu, sauts en rouge pointillé).

Limite : le trajet caché est un segment **direct** (échantillonné en points
cousus) ; il n'est garanti *sous* la broderie que pour des colonnes reliées par
une jonction (ou en quasi-contact). Un routage suivant réellement la matière
viendra avec le tatami avancé (Lot 7).

## Satin Coverage Analyzer : mesurer objectivement la surface couverte (2026-08-13)

*État : Présent (`satin_coverage::analyze_satin_coverage`) · Testé numériquement
(7 cas géométriques à résultat connu) · Observateur indépendant, n'influence
encore aucune décision du générateur.*

### Origine

L'audit des ruptures de ruban (§ *Saut plutôt qu'un point continu...* et
*Seuil angle/distance...* ci-dessus) et le repli tatami sur branche rejetée
(§ précédente) partagent un point aveugle commun : aucun des deux ne répond,
de façon indépendante et vérifiable, à la question « les colonnes générées
couvrent-elles réellement l'intégralité de la forme d'origine ? ». Les
validations existantes portent sur la cohérence des rails, du squelette, des
largeurs ou des jonctions — jamais sur une comparaison de SURFACE entre la
région source et ce que les colonnes balaient effectivement. Un squelette
valide, des sections toutes construites sans refus, et pourtant une zone
entière peut rester sans le moindre point : rien dans les validations
structurelles précédentes ne le détecte directement.

### Principe : comparer deux surfaces, jamais une heuristique de squelette

`satin_coverage::analyze_satin_coverage(target, columns, config)`
(`libs/satin_coverage/`, nouvelle bibliothèque, dépend uniquement de `core`,
`geometry`, `stitch_generation` — jamais de `auto_satin` ni `document`, pour
rester un **observateur totalement indépendant du générateur**, utilisable
depuis un test synthétique aussi bien que depuis `autodigitize`) calcule :

1. pour chaque colonne, la suite ordonnée de stations (Lᵢ, Rᵢ) via
   `stitch_generation::satin_stations` (nouvelle fonction publique, extraite
   sans changement de comportement de la correspondance ladder déjà utilisée
   par `fill_satin_columns` — cf. *Correction de l'appariement* plus haut) :
   la géométrie STRUCTURELLE des rails/barreaux, jamais les points de couture
   finaux (compensation pull, split-stitch, terminaisons), qui répondent à un
   besoin de couture et non à la question « les rails couvrent-ils la forme
   d'origine ? ». Une rupture de ruban (`SatinStation::jump_before`, § audit
   lettres/seuil angle-distance ci-dessus) coupe la colonne : aucun
   quadrilatère de couverture ne relie deux stations de part et d'autre d'un
   saut — le même mécanisme qui empêche un point cousu disproportionné
   empêche aussi un quadrilatère de couverture artificiel ;
2. chaque intervalle entre deux stations consécutives devient un quadrilatère
   (Lᵢ, Rᵢ, Rᵢ₊₁, Lᵢ₊₁), décomposé en DEUX TRIANGLES (diagonale Lᵢ-Rᵢ₊₁) —
   robuste à un quadrilatère non convexe, et surtout : un garde-fou défensif
   (`segments_cross_strict` sur Lᵢ-Rᵢ vs Lᵢ₊₁-Rᵢ₊₁) exclut et COMPTE tout
   intervalle où les deux rails se croiseraient, plutôt que de laisser un
   quadrilatère dégénéré s'ajouter silencieusement à l'union — l'analyseur ne
   fait jamais confiance aveuglément à l'invariant anti-croisement de
   l'appariement amont, même si celui-ci est déjà testé séparément ;
3. tous les triangles de toutes les colonnes sont réunis par une union
   booléenne NonZero (`geometry::union_nonzero`) → `C`. Chaque triangle est
   explicitement réorienté CCW avant l'union : `union_nonzero`, contrairement
   à `intersect_polygons`/`difference_polygons` (ajoutés par ce lot, voir plus
   bas), ne renormalise pas l'orientation d'entrée — deux triangles
   chevauchants d'orientations opposées s'annuleraient localement sous
   NonZero, créant un trou parasite qui masquerait précisément le genre de
   défaut que cet outil doit révéler (piège identifié dès la conception, pas
   trouvé après coup) ;
4. `C` est comparé à la région cible `P` (`geometry::PathSet`, trous compris)
   par intersection/différence booléennes NonZero, trous compris DES DEUX
   CÔTÉS (`geometry::intersect_polygons`/`difference_polygons`, nouvelles
   primitives de `libs/geometry` — contrairement à `subtract_polygons`
   existant, dont les `cutouts` sont de simples contours sans trou, ces deux
   nouvelles fonctions gèrent le cas d'une poche non couverte elle-même
   entourée de couverture de tous côtés, c'est-à-dire un TROU dans `C`) :
   `covered = aire(P ∩ C)`, `missing = aire(P \ C)`, `outside = aire(C \ P)`.

### Composantes manquantes et profondeur des trous

Chaque composante connexe de `missing` est déjà un élément distinct du
`vector<PathSet>` renvoyé par `difference_polygons` (issu directement du
`PolyTree64` de Clipper2) : aucun algorithme de composantes connexes séparé
n'est nécessaire. Pour chacune (`MissingRegion`) : aire, part de la cible,
centroïde/bbox (barycentre des sommets), et un `max_gap_radius_mm` — le rayon
du plus grand disque inscrit, obtenu par **recherche binaire d'érosions
successives** (`geometry::inset_path_set`, déjà présent, déjà testé) jusqu'à
disparition de la région, plutôt qu'un raster + transformée de distance
OpenCV : précision continue, aucune dépendance nouvelle, et cette bibliothèque
reste sans dépendance OpenCV. Distingue une frange fine de plusieurs mm² le
long d'un bord (rayon petit, peu grave) d'une poche ronde de même aire en
plein milieu (rayon significatif, vrai défaut).

Une **couverture du cœur** (`core_coverage_ratio`) complète la couverture
brute : `P` est érodé de `boundary_tolerance_mm` (0,1 mm par défaut) avant de
recalculer le manquant — absorbe les approximations de discrétisation le long
du bord sans masquer une vraie poche interne. Sur une forme trop fine pour
survivre à cette érosion, repli documenté sur la couverture brute plutôt
qu'une division par zéro.

### Rapport et seuils explicites

`SatinCoverageReport` (aires cible/couverte/manquante/hors-forme, ratios brut
et cœur, régions manquantes triées par aire décroissante, rayon de trou
maximal, `passed`, message texte diagnostic prêt à logger — jamais un simple
« couverture insuffisante », toujours les chiffres et la raison précise du
refus) et `SatinCoverageConfig` (tous les seuils : couverture brute/cœur
minimales, aire/rayon de trou maximaux, ratio hors-forme maximal, tolérance
de bord, résolution de la recherche de rayon — valeurs par défaut
raisonnables, explicitement PAS des constantes universelles du métier,
ajustables sans toucher au code de l'analyseur).

### Vérifié

Sept cas géométriques à résultat connu (`tests/unit/satin_coverage/test_coverage.cpp`,
43 assertions) : rectangle entièrement couvert (~100 %, PASS) ; rectangle
couvert à moitié (~50 %, FAIL, une seule région manquante de l'aire attendue) ;
trou de la forme source (anneau tuilé par quatre colonnes) jamais compté comme
manquant ; vraie poche non couverte entourée de matière (FAIL, rayon de trou
mesuré conforme au rectangle de la poche à 0,05 mm près) ; débordement hors
cible (`outside_area` > 0, cible elle-même toujours entièrement couverte) ;
deux colonnes qui se chevauchent (l'union fait foi, aucun double comptage) ;
et surtout **`trident`** (jonction concave asymétrique à 3 branches inégales,
forme historiquement problématique pour Auto-Satin, cf. audits ci-dessus) :
couverture complète (toutes branches) 88,1 %, plus grande zone manquante
18,0 mm² (le noyau de jonction résiduel déjà documenté § *Optimisation globale
des bridges...* — un résidu CONNU, pas un défaut de cet audit) ; en retirant
la branche PRINCIPALE (simulant un rejet amont), couverture 9,4 %, plus grande
zone manquante 157,8 mm² — démonstration que l'analyseur distingue nettement
« toutes les branches présentes » de « une branche absente », par une mesure
de surface et non un décompte de branches/stations traitées. Suite complète
(514 cas ctest) sans régression, Debug et Release, y compris la suite
`stitch`/`auto_satin`/`autodigitize` complète après l'extraction de
`satin_stations` (comportement bit-à-bit identique à l'ancien
`fill_satin_columns`).

**Portée actuelle et limites assumées** : observateur pur, n'influence encore
aucune décision de génération. Pas de second mode « couverture des points
finaux » (§16 de la demande d'origine). `degenerate_interval_count` reste à 0
sur toutes les fixtures testées à ce jour (l'invariant anti-croisement amont
tient) ; le garde-fou existe pour le jour où ce ne sera plus le cas plutôt que
pour un défaut déjà observé.

### Non-régression sur le corpus de formes historiques

*État : Présent (`tests/unit/auto_satin/test_coverage_regression.cpp`).*

Photographie la couverture géométrique actuelle de dix formes du corpus
`auto_satin::shapes` (`rectangle`, `capsule`, `ribbon`, `s`, `y`, `t`,
`cross`, `h`, `ring`, `trident` — `circle`/`tiny`/`notch`/`pinch` exclus,
testés ailleurs pour leur comportement de refus, pas pour leur couverture) :
un plancher de couverture brute par forme, mesuré en calibrant ce test puis
abaissé de quelques points de marge — un vrai plancher de non-régression,
**pas** un objectif de qualité (les résidus de noyau de jonction sur
`y`/`t`/`cross`/`h`/`trident`, § *Optimisation globale des bridges...* et §
*Séparation StableBranchEnd/JunctionSeparator...* plus haut, sont un fait déjà
documenté, pas un défaut que ce test cherche à faire disparaître). Sans
surprise, `rectangle`/`ring` sont quasi parfaits (96 %/99,99 %) ; les formes à
jonction se situent entre 85,8 % (`cross`) et 88,7 % (`t`).

`wide` (rectangle délibéré 100 × 20 mm, `shapes.cpp` : « un vrai cas trop
large pour satin ») refuse entièrement dès l'analyse de satinabilité
(`Unsuitable`, `has_wide_area` : sa largeur de 20 mm dépasse `max_satin_width`,
9 mm par défaut, `satinability.cpp:76`) — **comportement voulu par
construction**, pas un défaut. Exclue du corpus de ce test pour la même
raison que `circle`/`tiny`/`notch`/`pinch`.

### Diagnostic « wide » (2026-08-13) : confirmation, pas de défaut trouvé

*Investigation demandée explicitement pour vérifier l'hypothèse ci-dessus.*
Trace complète : `evaluate_satinability` (`satinability.cpp:76`) calcule
`maxWidthUm` à partir de la transformée de distance du raster de la région
(le rayon maximal du disque inscrit, doublé) ; pour `wide` (100 × 20 mm),
cette largeur maximale est bien ~20 mm, très au-delà de `max_satin_width`
(9 mm, `SatinColumnsParameters::analysis.thresholds`) — `has_wide_area` passe
vrai, `status` devient `Unsuitable`, message « Zone trop large pour un satin
(envisagez un tatami). », et `build_satin_columns` s'arrête avant toute
construction de rail (`satin_column.cpp:2977`, refus immédiat sur statut
`Unsuitable`). Aucune trace d'un défaut de squelette, de section transversale
ou de jonction : le refus intervient AVANT que le pipeline géométrique ne
s'exécute, sur un seuil de configuration simple et correctement appliqué.
**Conclusion : `wide` fonctionne exactement comme conçu** (rediriger une
forme trop large vers un remplissage tatami, cf. § *Colonne trop large* plus
haut) ; la note précédente (« surprenant, piste non investiguée ») provenait
d'une lecture incomplète de `shapes.cpp` avant d'avoir vérifié le seuil
réellement en cause — corrigée ici plutôt que laissée en l'état.

### Diagnostic « trident »/pixel_size (2026-08-13) : pas de perte de couverture réelle, fragilité Legacy documentée

*Investigation demandée explicitement, en plusieurs passes (deux hypothèses
de correctif tentées et invalidées par la mesure avant la conclusion
ci-dessous — aucune des deux n'a été committée).*

**Symptôme observé** (`openstitch-cli auto-satin-debug --shape trident`,
mode Legacy, `--satin-geometry=legacy`) : à `pixel_size = 0,05 mm` (défaut
CLI), la branche latérale de `trident` échoue («&nbsp;colonne refusée : saut
de largeur incohérent entre stations finales #1/2&nbsp;»), ce qui fait
échouer la validation de jonction («&nbsp;jonction 1 incomplète, 2/3 branches
disponibles&nbsp;») et refuse **toute la région**. À `pixel_size = 0,1 mm`
(convention des fixtures de test), la même forme réussit (88,1 % de
couverture).

**Deux hypothèses testées, toutes deux invalidées par instrumentation
directe** (dump des largeurs de station réelles, `OS_DEBUG_STATIONS`,
retiré avant commit) :

1. *Pointe effilée mal exemptée près du bout OUVERT.* Correctif tenté :
   n'appliquer le contrôle de saut de largeur qu'au tiers médian de l'axe
   (mêmes bornes que `representative_station_width`). **Invalidé** : la
   branche fautive ne compte que 5 stations après amputation de jonction, un
   tiers médian trop étroit pour exempter l'intervalle fautif — aucun
   changement observé après correctif.
2. *Amputation de jonction qui s'arrête trop tôt faute de palier stable.*
   Correctif tenté : étendre `trim_unstable_junction_tail` pour continuer
   d'amputer tant que la frontière reste incohérente avec sa voisine
   immédiate (même seuil que la validation séparée). **Invalidé** :
   l'intervalle fautif (stations #1/#2, largeurs 881 µm puis 4393 µm) se
   situe près du bout **OUVERT**, pas du bout de jonction — `trim_unstable_
   junction_tail` n'amputant QUE côté jonction par construction, aucune
   amputation supplémentaire ne pouvait l'atteindre. Widths réelles observées
   sur cette branche : 807,5 → 881,1 → **4393,0** → 7606,2 → 9423,7 µm — un
   saut brutal et isolé juste après la pointe, puis une remontée globalement
   cohérente vers la confluence.

**Cause probable** (non creusée plus loin, cf. portée ci-dessous) : la
station à 4393 µm correspond à la station d'axe #23, signalée ailleurs comme
« interpolée » (échec isolé de `cross_section` comblé par interpolation
linéaire, § *Génération partielle sur formes concaves*). Sur cette branche
courte et proche d'une confluence très asymétrique, la section transversale
calculée depuis la seule tangente locale peut basculer brutalement de
« mesure de cette branche seule » à « balayage de la branche voisine bien
plus large », dès la station suivant la pointe — un phénomène de bord
plausible pour une géométrie aussi contrastée, mais dont le mécanisme exact
dans `cross_section` n'a pas été tracé en détail.

**Conclusion — pas de correctif nécessaire, car pas de perte de couverture
réelle.** Relecture de `libs/autodigitize/src/autodigitize.cpp` :

1. `autodigitize.cpp` utilise le mode **`Parametric`** par défaut
   (`geometry_mode = SatinGeometryMode::Parametric`, ligne 189), jamais
   `Legacy` — alors que le CLI `auto-satin-debug` testé ci-dessus utilise
   `Legacy` par défaut (`--satin-geometry=legacy`). Le symptôme observé n'a
   donc jamais été observé dans le chemin réellement emprunté par la
   numérisation automatique.
2. Même le mécanisme de refus en cascade (« jonction incohérente ») qui
   transforme un échec de branche isolé en refus de LA RÉGION ENTIÈRE est
   spécifique à `Legacy` : le mode `Parametric` n'a « aucune ancre centrale
   commune, aucun sommet de bissectrice... » (§ *Jonctions : recouvrement
   local, sans ancre* plus haut) — un échec de branche y resterait
   individuel, sans faire échouer les deux autres.
3. Et si malgré tout `build_satin_columns` refusait la totalité (`columns` ET
   `parametric_columns` vides), `autodigitize.cpp` (ligne 300-315) retombe
   automatiquement sur un remplissage **tatami de la région entière** dès que
   `sectionCount == 0` — aucune zone sans point, juste un changement de type
   de point (satin → tatami).

Trois filets indépendants (mode par défaut différent de celui testé,
refus non cascadé en mode Parametric, repli tatami région entière en dernier
recours) protègent donc la couverture livrée à l'utilisateur, même dans le
pire des cas où cette fragilité de construction de colonne Legacy se
manifeste. **Rien à corriger côté couverture** ; la fragilité elle-même
(refus d'une branche courte proche d'une confluence très asymétrique, à
certaines résolutions de raster, en mode Legacy) reste une piste
d'amélioration future légitime de robustesse — pas urgente, et hors du
périmètre de cet audit de couverture.

### Balayage du corpus de formes (mode × pixel_size) : deux défauts réels trouvés et corrigés (2026-08-13)

*État : Présent, corrigé.* `geometry::inset_path_set` (`libs/geometry/src/offset.cpp`)
et le câblage `--coverage-svg` du CLI (`apps/cli/main.cpp`).

Utilisation directe de l'analyseur pour balayer les 15 formes du corpus
`auto_satin::shapes`, en Legacy ET Parametric, à `pixel_size` 0,05 et 0,1 mm
(60 exécutions, `openstitch-cli auto-satin-debug --coverage-svg`) — pas
encore automatisé en test, exploration manuelle guidée par les chiffres bruts.
Deux défauts RÉELS trouvés, tous deux corrigés séance tenante, vérifiés sans
régression (517 cas ctest, Debug+Release) :

1. **`geometry::inset_path_set` ne réoriente pas les trous avant Clipper2**
   (contrairement à `boolean.cpp`, qui le fait déjà pour `subtract_polygons`
   et pour `intersect_polygons`/`difference_polygons` ajoutés par ce lot).
   `InflatePaths` offsette chaque contour par rapport à SA PROPRE
   orientation : un trou de même orientation que son extérieur (au lieu de
   l'opposée) grandit ou rétrécit à l'envers lors d'une érosion. Repéré sur
   `ring` (`shapes.cpp`, dont le trou et l'extérieur sont générés par le même
   parcours d'angle croissant de `circle()`) : couverture brute correcte
   (99,99 %, les opérations d'union/différence QUE cet audit a ajoutées se
   réorientent déjà correctement) mais **couverture du cœur mesurée à
   71,18 %** au lieu d'un ~100 % attendu — rien d'autre dans le pipeline
   Auto-Satin n'étant sensible à l'orientation d'un trou, ce défaut latent
   était invisible avant que `core_coverage_ratio` (qui appelle
   `inset_path_set` sur la région cible) ne l'expose. Corrigé par le même
   motif `oriented_as` que `boolean.cpp` (dupliqué localement, ADR-005) ;
   `shapes.cpp` corrigé en complément (trou de `ring` réorienté à la
   construction) pour que la fixture soit correcte par construction, pas
   seulement tolérée par une réorientation défensive en aval. Nouveau test
   dédié (`tests/unit/geometry/test_offset.cpp`, anneau carré dont le trou
   partage délibérément l'orientation de l'extérieur) vérifiant l'aire nette
   après érosion. Après correctif : `ring` couverture cœur 100,00 %, PASSED.
2. **Le câblage `--coverage-svg` du CLI lisait `parametric_columns` sur le
   seul indicateur `--satin-geometry` demandé**, pas sur la liste
   RÉELLEMENT peuplée par `build_satin_columns` — alors que certaines formes
   (anneaux, cas refusés en Parametric) retombent automatiquement sur
   `columns` (Legacy) À L'INTÉRIEUR de `build_satin_columns` même quand
   Parametric est demandé (§ *Objets satin paramétriques* plus haut).
   Symptôme : `ring --satin-geometry=parametric` rapportait 0 colonne, 0 %
   de couverture, alors que des colonnes existaient bel et bien (côté
   `columns`, jamais lu). Corrigé en testant `!parametric_columns.empty()`
   plutôt que l'indicateur demandé — même motif que `autodigitize.cpp`
   (`useParametric`, déjà correct). Bug de câblage CLI, jamais de défaut
   Auto-Satin lui-même ; le bloc pré-existant de génération du SVG
   `--output-svg` (`parametric_to_svg`) partage la même faiblesse mais n'a
   pas été touché ici (hors périmètre de cet audit, à corriger séparément).

**Piste non investiguée, potentiellement plus significative que les deux
ci-dessus** : `notch` (bande à encoche en V profonde) montre un écart
systématique et notable entre modes — couverture brute **~86-87 % en
Legacy contre ~77-78 % en Parametric**, alors que Parametric est le mode
utilisé par défaut dans `autodigitize.cpp` (contrairement au cas `trident`
ci-dessus, aucun filet ne compense ici puisque Parametric produit bien une
colonne, juste avec une couverture dégradée). Zone manquante la plus grande
mesurée jusqu'à 39,4 mm² (22 % de la cible) selon la résolution de raster.
Hypothèse de départ, non vérifiée : l'ajustement Bézier à paires structurantes
éparses (`select_structural_indices`/`fit_both_rails`) capturerait moins
fidèlement une encoche concave profonde que l'échantillonnage dense de
`Legacy`. À investiguer en profondeur dans une prochaine session (tracer
`fit_both_rails`/l'erreur d'ajustement autour de l'encoche, comme fait ici
pour `trident`/pixel_size) avant toute tentative de correctif.

### Diagnostic sur image réelle (`tentabrode.png`) : le repli tatami ne protège pas toutes les régions (2026-08-13)

*État : Présent, diagnostic seul (aucun correctif) — test permanent*
`TEST_CASE("DIAGNOSTIC TEMPORAIRE couverture satin (tentabrode)")`
(`tests/integration/test_pipeline.cpp`), toujours en échec (`CHECK(false)`),
même convention que les diagnostics GISTRE existants.

Applique l'analyseur à la sortie réelle d'`autodigitize::auto_digitize` sur
`tests/fixtures/tentabrode.png` (pas une forme synthétique du corpus), en
regroupant par `source_region` (`document::VectorObject::source_region`) pour
combiner chaque vecteur satin avec son éventuel repli tatami — le filet décrit
plus haut (§ *Repli tatami sur une branche auto-satin rejetée*) ne se déclenche
QUE sur le signal structurel `network.debug.graph.edges.size() > sectionCount`
(branche entière rejetée), jamais sur une couverture partielle d'une branche
par ailleurs acceptée.

Sur les 14 régions satin-portantes de cette image :

- **Couverture satin seule (sans repli), agrégée sur toute l'image : 21,3 %.**
  Ce chiffre seul est trompeur — il ignore le filet tatami — mais confirme que
  l'écart entre colonnes générées et surface cible n'est pas anecdotique sur
  une image réelle.
- **Couverture réelle (satin + repli tatami combiné par région), agrégée :
  97,3 %.** Le filet comble donc l'essentiel de l'écart, comme conçu.
- **Mais 9 des 14 régions restent sous le seuil de 99,5 % même après
  combinaison**, dont deux sévèrement et **sans aucun repli** (le signal
  structurel qui déclenche le filet n'a pas été franchi) : un vecteur à
  **46,6 %** et un autre à **59,3 %** de couverture réelle.

Ce résultat corrobore et QUANTIFIE, sur une image réelle, la même catégorie de
défaut que la piste `notch`/Parametric non investiguée ci-dessus (écart de
couverture sur une branche acceptée, pas un rejet structurel) : le filet
tatami actuel protège contre le cas « branche entièrement refusée » mais pas
contre le cas « branche acceptée avec une géométrie de colonnes qui ne couvre
pas fidèlement sa région », qui semble être la situation la plus courante en
pratique sur des formes réelles bruitées. C'est le constat central qui motive
la refonte d'architecture décrite ci-dessous (§ *Décomposition guidée par
squelette*) : plutôt que d'élargir encore le filet de repli, traiter la
décomposition d'une région branchée en sous-régions satinables comme une étape
de planification à part entière, en amont du générateur actuel.

### Visualisation de debug (`coverage_to_svg`)

*État : Présent · Câblé dans `openstitch-cli auto-satin-debug --coverage-svg`.*

`satin_coverage::coverage_to_svg(target, columns, report)` superpose, mêmes
conventions que `auto_satin::debug_export` (millimètres, Y inversé) : la
couverture en vert translucide, les zones manquantes en rouge (une par
composante connexe, `fill-rule="evenodd"` pour respecter leurs propres
trous), le débordement en orange, le contour de la cible en gris, et pour
chaque colonne ses deux rails (bleu/orange) et ses stations structurelles
(points — rouges aux stations atteintes par un saut, cf.
`SatinStation::jump_before`). Un commentaire d'en-tête résume le rapport en
texte (aires, ratios, nombre de régions manquantes), lisible sans rendu
graphique, plus une ligne par région manquante (aire, part de la cible, rayon
de trou, centroïde).

`openstitch-cli auto-satin-debug --shape <forme> --coverage-svg <fichier>`
calcule la couverture des colonnes réellement construites (Legacy ou
Parametric selon `--satin-geometry`) et écrit à la fois le diagnostic texte
sur la sortie standard et ce SVG. Vérifié en conditions réelles sur `trident` :
à la résolution de raster par défaut (0,05 mm/px), le mode Legacy REFUSE
entièrement la forme (« jonction 1 incomplète, 2/3 branches disponibles ») —
le SVG rend alors 100 % de la cible en rouge, cohérent avec zéro colonne
produite ; à 0,1 mm/px, 88,14 % de couverture avec quatre composantes
manquantes (la plus grande, 18,05 mm², est le noyau de jonction déjà
documenté). Cette sensibilité de `trident` à la résolution de raster en mode
Legacy n'était pas documentée avant cet outil ; investiguée en profondeur
ci-dessous (§ *Diagnostic « trident »/pixel_size*) — conclusion : sans impact
réel sur la couverture livrée à l'utilisateur, grâce aux filets déjà en place
en amont de ce chemin CLI brut.

Testé (`tests/unit/satin_coverage/test_coverage.cpp`) : SVG bien formé
(`<svg>`…`</svg>`), couleurs de remplissage vert/rouge et rails bleu/orange
effectivement présents pour un cas de couverture partielle connu.

## Décomposition guidée par squelette (SGSD) : planification globale au-dessus de l'Auto-Satin (2026-08-13)

### Motivation : une forme = une colonne est une hypothèse trop restrictive

*État : En cours — phase 1 sur 9 livrée (planification topologique pure,
aucune génération de géométrie).*

`build_satin_columns` (§ *Colonnes automatiques par squelette* plus haut)
traite déjà une région branchée en construisant une colonne par arête du
squelette, mais TOUJOURS par rapport au contour ORIGINAL non découpé : les
sections transversales de chaque branche sont mesurées contre la même
`geometry::PathSet` entière, jamais contre une sous-région dédiée. Les
jonctions sont donc résolues a posteriori par des heuristiques locales
(`trim_unstable_junction_tail`, `StableBranchEnd`/`JunctionSeparator` en
Legacy, recouvrement borné en Parametric) qui amputent ou recouvrent, mais ne
peuvent jamais réellement replanifier la forme. Le diagnostic `tentabrode.png`
ci-dessus montre l'impact réel de cette limite : des régions acceptées (pas
rejetées) dont la couverture reste significativement incomplète, sans qu'aucun
filet ne s'en aperçoive.

L'objectif de la SGSD (*Skeleton-Guided Satin Decomposition*) est d'introduire
un étage de PLANIFICATION avant l'Auto-Satin existant, qui devient un solveur
LOCAL : découper une région branchée en un petit nombre de sous-régions
simples (chacune proche d'un chemin squelette `endpoint — endpoint`, sans
jonction interne), construire une colonne satin par sous-région avec le
générateur actuel INCHANGÉ, mesurer la couverture de chaque sous-région et de
l'assemblage avec `satin_coverage` (déjà générateur-agnostique, § plus haut),
puis fusionner/router. L'Auto-Satin existant n'est ni supprimé ni modifié : il
est réutilisé tel quel comme générateur d'une région simple, et comme oracle
de qualité pour comparer des découpages candidats.

Déploiement en 9 phases, chacune testable et commitée séparément, sans jamais
toucher au générateur de rails existant avant que la décomposition elle-même
soit validée :

1. **Graphe de squelette + appariement de branches (diagnostic seul).**
2. **Décomposition du graphe en `SatinPath`** (chemins topologiques maximaux)
   — livrée avec la phase 1, § *Phase 1* ci-dessous.
3. **Génération de candidats de coupe + découpage réel du polygone en
   `SatinRegion`.**
4. **`SatinabilityAnalyzer`** (la région candidate est-elle satinable ?) —
   livrée, § *Phase 4* ci-dessous.
5. **Auto-Satin par région + Coverage Analyzer comme oracle de sélection** —
   livrée, § *Phase 5* ci-dessous.
6. **Recherche à faisceau (*beam search*) sur les découpages candidats** —
   livrée (portée réduite à une décision de coupe à la fois, § *Phase 6*
   ci-dessous), aucune recherche combinatoire multi-jonctions.
7. **Passe de fusion (*merge*) post-découpage pour minimiser le nombre de
   segments** — livrée (une seule passe, pas de fusion en cascade, § *Phase 7*
   ci-dessous).
8. **Recouvrements (*overlaps*) entre régions adjacentes + génération de
   géométrie de couture** — livrée (paires directement adjacentes
   uniquement, § *Phase 8* ci-dessous).
9. Routage multi-colonnes (continuité directe / travel / jump / trim).

### Phase 1 : réutilisation du `SkeletonGraph` existant + appariement de branches (`libs/satin_planning`)

*État : Présent, testé, aucune régression.*
`libs/satin_planning/include/openstitch/satin_planning/branch_pairing.hpp` +
`src/branch_pairing.cpp` (nouvelle bibliothèque, ne dépend que de
`openstitch::core` et `openstitch::auto_satin` — jamais l'inverse, la
couche `auto_satin` reste indépendante de la planification).

Le graphe de squelette voulu par la SGSD (nœuds typés `Endpoint`/`Junction`,
arêtes portant une centerline et des rayons locaux) EXISTAIT DÉJÀ presque
intégralement avant cette session :
`auto_satin::SkeletonGraph`/`SkeletonNode`/`SkeletonEdge`
(`libs/auto_satin/include/openstitch/auto_satin/skeleton_graph.hpp`),
construit par `build_skeleton_graph` et élagué par `prune_graph`
(`graph_cleanup.hpp`), tous deux déjà exposés via
`auto_satin::analyze_region(region, params).debug.graph` (graphe élagué,
déterministe). La phase 1 ne réimplémente donc RIEN de cette partie — elle
consomme directement ce graphe existant. Ce qui manquait réellement : une
façon de décider, à chaque jonction, quelles branches se prolongent
naturellement l'une l'autre plutôt que de traiter chaque arête indépendamment.

**Coût de continuation** (`continuation_cost`, `ContinuationCostWeights` —
poids `angle=1.0`, `width=0.6`, `curvature=0.4`, tout centralisé, aucun
coefficient dispersé) entre deux arêtes incidentes à une même jonction :

- `angle_cost` = `(1 + cos θ) / 2` où `θ` est l'angle entre les tangentes
  SORTANTES des deux branches (moyenne pondérée par longueur sur une fenêtre
  de `tangent_window_um` = 1,5 mm depuis la jonction, cohérent avec
  `GraphCleanupParameters::minimum_branch_length`) : 0 pour une continuation
  en ligne droite (tangentes opposées), 1 pour un repli sur soi-même.
- `width_cost` = `|log(rayonA / rayonB)|` (rayon local moyen sur la même
  fenêtre) — différence RELATIVE plutôt qu'absolue, pour qu'une transition
  4,1 mm→4,0 mm coûte presque rien alors qu'une transition 1 mm→8 mm coûte
  cher.
- `curvature_cost` = stabilité de la tangente sur la fenêtre (écart entre le
  premier et le dernier segment échantillonné) — pénalise une branche qui
  tourne déjà fortement près de la jonction.

**Résolution d'une jonction** (`pair_branches_at_junction`) : toutes les
paires d'arêtes incidentes sont testées (`C(degré, 2)` candidats, triés par
coût croissant) et la moins coûteuse devient la continuation principale
(`selected_pair`) ; les autres arêtes restent `detached` — chacune devient
l'extrémité d'un `SatinPath` secondaire. Pour un degré 3 (T, Y), c'est un choix
entre 3 appariements. Pour un degré ≥ 4 (croix), stratégie délibérément
simple : une seule continuation principale, toutes les autres branches
détachées individuellement — les appariements multiples simultanés (ex.
`A↔C` ET `B↔D`) sont reportés à une phase ultérieure car ils peuvent produire
des colonnes qui se croisent géométriquement au centre de la jonction, ce que
la phase 1 (aucune géométrie générée) ne peut pas encore vérifier.

**Décomposition complète** (`decompose_into_paths`) : parcourt tous les nœuds
`Junction`, construit la carte des continuations retenues, puis assemble des
`SatinPath` maximaux en suivant les appariements de proche en proche —
partition EXACTE des arêtes du graphe (chaque arête apparaît dans exactement
un chemin, vérifié par test sur tout le corpus de formes). Un pont
Jonction-Jonction (le cas `h`, § plus haut) qui continue naturellement aux
DEUX bouts fusionne les deux montants et le pont en un seul chemin, exactement
la même intuition que la conversion pont→colonne déjà en place côté
générateur. `format_decomposition_report` produit un rendu texte structuré
(jonction par jonction, candidats avec leurs coûts, sélection, chemins
résultants) pour le debug — aucun `fprintf` direct dans la bibliothèque.

Aucune géométrie n'est générée à cette phase : ni découpage de polygone, ni
appel à `build_satin_columns`. C'est une étape de planification topologique
pure, décorrélée du générateur, exactement comme demandé.

**Testé** (`tests/unit/satin_planning/test_branch_pairing.cpp`, 12 cas) :

- Cas synthétique fait main (jonction en T à trois branches, coûts calculés à
  la main) vérifiant numériquement les trois formules de coût.
- Formes réelles du corpus `auto_satin::shapes` passées par le VRAI pipeline
  de rasterisation/squelettisation (`analyze_region`) : `rectangle` (0
  jonction, 1 chemin), `t` (1 jonction degré 3, 2 chemins), `cross` (1
  jonction degré 4, 1 continuation + 2 détachées, 3 chemins), `h` (2
  jonctions, le pont détaché aux deux bouts confirmé par introspection du
  graphe, 3 chemins), `trident` (1 jonction degré 3, 2 chemins).
- Propriété de partition (chaque arête dans exactement un chemin) vérifiée
  génériquement sur 12 formes du corpus.
- Rendu `format_decomposition_report` non vide et contenant les marqueurs
  attendus.

### Phase 3 : candidats de coupe + découpage réel du polygone (`libs/satin_planning/region_split`)

*État : Présent, testé, aucune régression — v1 purement géométrique (pas
encore d'oracle Auto-Satin, cf. phase 5).*
`libs/satin_planning/include/openstitch/satin_planning/region_split.hpp` +
`src/region_split.cpp`.

Cette phase transforme chaque `SatinPath` (chemin topologique, phase 1) en un
véritable sous-polygone (`SatinRegion`), en réutilisant tel quel l'outil de
coupe manuelle déjà existant (`geometry::cut_path_set`, § *Ligne de coupe
manuelle* plus haut) plutôt que d'écrire un nouveau moteur de découpe — la
seule brique manquante était de choisir AUTOMATIQUEMENT où et dans quelle
direction couper.

**Un point important sur `cut_path_set`** : ce n'est jamais une entaille
locale — la coupe est toujours prolongée en une ligne complète qui traverse
toute la boîte englobante de la région, quels que soient les points fournis
(pensé pour un geste utilisateur qui vise la jonction sans forcément atteindre
les bords exacts). Un candidat de coupe automatique doit donc être validé
géométriquement avant d'être accepté, pas seulement calculé : la ligne peut en
principe traverser une zone sans rapport si la forme n'est pas localement
simple près de la jonction.

**Recherche de candidats** (`generate_cut_candidates`) : pour une branche
détachée (jonction + arête, directement issues de `JunctionPairingReport::detached`),
balaie des distances croissantes depuis la jonction
(`search_min_um`=300&nbsp;µm à `search_max_um`=1500&nbsp;µm par pas de
150&nbsp;µm, §11 spec SGSD — jamais exactement au pixel de jonction). À
chaque distance, calcule le point et la tangente locale sur la centerline,
construit la ligne de coupe selon la normale, et appelle `cut_path_set`.
Rejette immédiatement (§13 spec SGSD, sous-ensemble géométrique — l'oracle
Auto-Satin complet est phase 5)&nbsp;:

- un résultat qui n'a pas EXACTEMENT 2 morceaux (ligne qui a traversé une zone
  sans rapport) ;
- un fragment plus petit que `min_piece_area_mm2` (0,3&nbsp;mm² par défaut).

Le premier candidat valide (le plus proche de la jonction) est retenu — un
choix délibérément simple pour cette v1 géométrique pure ; la sélection par
score global (§13 complet, oracle de couverture) est reportée à la phase 5/6,
une fois `SatinabilityAnalyzer` et l'intégration Auto-Satin par région en
place.

**Découpage complet** (`split_region`) : traite chaque événement de
détachement (une jonction, une arête de son `detached`) dans un ordre
déterministe (jonction puis arête croissantes), en localisant à chaque fois
le morceau COURANT du pool qui contient le point de jonction (test
point-dans-polygone par ray casting, implémenté localement — les chemins
manipulés ici sont toujours polygonaux, jamais des courbes de contrôle) avant
d'y appliquer la coupe retenue. Le morceau découpé est remplacé par les deux
résultants dans le pool. **Aucun traitement spécial n'est nécessaire pour une
branche à deux bouts de jonction** (le pont d'un « H ») : le second événement
de détachement retrouve naturellement, dans le pool, le morceau laissé par le
premier — la même mécanique générique s'applique. Assignation finale : chaque
`SatinPath` reçoit le morceau du pool qui contient un point sonde intérieur à
son propre tracé (un nœud strictement interne au chemin s'il y en a un, sinon
le milieu de ses deux extrémités — jamais un point exactement sur une coupe).
Une branche qu'aucune coupe valide ne sépare reste fusionnée avec son morceau
parent, reportée dans `unresolved_paths` plutôt que silencieusement ignorée.

**Testé** (`tests/unit/satin_planning/test_region_split.cpp`, 4 cas) contre le
VRAI pipeline de rasterisation/squelettisation, pas de fixtures synthétiques
faites main pour cette phase — la géométrie réelle du contour est indispensable
ici (contrairement à la phase 1, purement topologique) :

- `rectangle` (0 jonction) : aucune coupe tentée, la région ressort inchangée.
- `t` : une coupe à la distance minimale testée (300&nbsp;µm) isole le pied
  (100,44&nbsp;mm²) de la barre (238,84&nbsp;mm²) sur les 9 candidats testés,
  tous valides et croissants avec la distance — confirme que le balayage se
  comporte de façon monotone et prévisible.
- Propriété générale sur `t`, `y`, `cross`, `h`, `trident` : **tous les
  chemins de ces cinq formes sont intégralement résolus** (`unresolved_paths`
  vide dans les cinq cas), y compris `h` (les deux coupes successives du pont
  fonctionnent sans logique dédiée) et `cross` (les deux branches détachées
  de l'unique jonction degré 4 sont isolées l'une après l'autre du même
  morceau restant). Aire cumulée des régions résolues toujours inférieure à
  l'aire d'origine (une coupe ne peut que retirer une fine bande, jamais en
  ajouter), `path_index` assignés uniques.
- Rendu `format_region_split_report` non vide et contenant les marqueurs
  attendus.

Aucune géométrie satin n'est générée à cette phase non plus — c'est un
découpage de polygone pur, décorrélé du générateur.

**Mise à jour (phase 4, voir ci-dessous) : la validation purement géométrique
ci-dessus s'est révélée insuffisante et a été renforcée**, pas remplacée —
`generate_cut_candidates` intègre désormais un garde-fou de satinabilité
(§ *Phase 4*) qui a directement corrigé un défaut réel trouvé sur `t`, `y` et
`cross` : une coupe jugée valide par les seuls critères géométriques
(exactement 2 morceaux, aucun fragment trop petit) pouvait en réalité trancher
AUSSI la branche voisine près d'une confluence où plusieurs branches se
chevauchent géométriquement, laissant une jonction résiduelle bien réelle
dans le morceau censé être isolé.

### Phase 4 : `SatinabilityAnalyzer` — et un défaut réel trouvé et corrigé dans la phase 3 (`libs/satin_planning/region_satinability`)

*État : Présent, testé, aucune régression.*
`libs/satin_planning/include/openstitch/satin_planning/region_satinability.hpp`
et `src/region_satinability.cpp`.

**Bonne surprise, même schéma qu'aux phases précédentes** : la spec SGSD (§14)
demande de « créer une vraie fonction `analyzeSatinability` » examinant le
nombre de jonctions/extrémités du squelette, la largeur, la variation de
largeur, etc. — exactement ce que `auto_satin::evaluate_satinability` /
`auto_satin::analyze_region` font déjà (§ *Analyse de satinabilité* plus
haut). Rien à réimplémenter : `check_region_satinability(region, params)`
fait simplement tourner le VRAI pipeline existant (rasterisation → distance →
squelette → graphe → `evaluate_satinability`) sur une `SatinRegion` déjà
découpée, exactement comme le ferait `build_satin_columns` en interne, et
classe le verdict `ready_for_generation` selon que le statut résultant est
`Suitable`/`SuitableWithWarnings`. `check_all_regions` applique ça à tout un
`RegionSplitReport` (phase 3), en reportant aussi automatiquement dans
`not_ready` les chemins jamais isolés (`unresolved_paths`) — pas de verdict
sans région, mais pas d'omission silencieuse non plus.

**Défaut réel trouvé en testant bout-en-bout (décompose + découpe + vérifie)
sur le corpus branché** : chaque `SatinRegion` fraîchement découpée à la
phase 3 était réanalysée isolément, et le résultat était systématiquement
`RequiresDecomposition` (une jonction résiduelle) pour `t`, `y`, `cross` ET le
pont d'un « H » — jamais `Suitable` comme attendu pour une branche qui vient
d'être proprement détachée. Diagnostic en trois temps (instrumentation
temporaire, retirée une fois la cause confirmée, même méthode que l'audit
`trident`/pixel_size) :

1. **D'abord écarté : artefact de rasterisation.** Un balayage de
   `pixel_size` de 50 à 6,25&nbsp;µm sur la région coupable ne change RIEN au
   résultat (`junctions=1` à toutes les résolutions) — élimine l'hypothèse
   d'un escalier de pixellisation au bord de la coupe (contrairement au cas
   `trident`/pixel_size documenté plus haut, qui LUI dépendait bien de la
   résolution).
2. **Cause confirmée : la coupe par défaut (300–1500&nbsp;µm de la jonction)
   tombe encore dans la zone où les empreintes de deux branches voisines se
   chevauchent.** Sur `t`, la branche du pied (2,5&nbsp;mm de demi-largeur)
   est testée à des distances qui restent À L'INTÉRIEUR de l'emprise en X de
   la barre horizontale (elles se croisent exactement à la jonction) : la
   ligne de coupe, bien que géométriquement valide au sens « exactement 2
   morceaux, aucun fragment trop petit », tranche EN RÉALITÉ un peu de la
   barre en même temps que le pied, laissant un résidu détectable comme une
   vraie troisième branche. Balayage direct de la distance de coupe
   (300&nbsp;µm à 7&nbsp;mm) : le morceau isolé reste branché
   (`RequiresDecomposition`) jusqu'à 2000&nbsp;µm inclus, et devient
   proprement `Suitable` (0 jonction) à partir de 2600&nbsp;µm — la marge par
   défaut de la phase 3 (max 1500&nbsp;µm) n'atteignait jamais cette
   distance.
3. **Corrigé** : `CutCandidateParams` élargit la plage de recherche par
   défaut (`search_max_um` 1500→4000&nbsp;µm, `search_step_um`
   150→300&nbsp;µm) ET `generate_cut_candidates` ajoute un garde-fou —
   quand le bout distal de la branche détachée est une VRAIE extrémité (pas
   une autre jonction), le morceau isolé par chaque candidat est réanalysé
   avec `auto_satin::analyze_region` et le candidat est rejeté si une
   jonction résiduelle subsiste, avant même de le proposer comme sélection.
   Le cas d'un bout de jonction (le pont d'un « H », § plus haut) est
   délibérément EXCLU de ce garde-fou : ce chemin a besoin de DEUX coupes
   successives, et le vérifier après la première le rejetterait à tort — il
   est encore légitimement branché à ce stade-là. Documenté comme limite
   connue plutôt que « corrigé » : une validation correcte du pont
   demanderait de ne réappliquer le garde-fou qu'après la DERNIÈRE coupe
   d'un chemin à deux bouts de jonction, une information que
   `generate_cut_candidates` (qui ne traite qu'un événement de détachement à
   la fois, sans mémoire du chemin complet) n'a pas.

**Vérifié après correctif** (`tests/unit/satin_planning/test_region_satinability.cpp`,
5 cas) :

- `rectangle` : déjà simple, prête sans découpe.
- `t`, `y`, `cross` (branches détachées se terminant toutes par une vraie
  extrémité) : **100&nbsp;% des régions ressortent propres**
  (`junction_count == 0`, `ready_for_generation`) — le défaut ci-dessus est
  bien corrigé pour ces trois formes.
- `h` : les deux montants (jamais coupés qu'à une seule extrémité chacun)
  ressortent propres ; le pont (les deux bouts sur une jonction) reste
  honnêtement classé `not_ready` — limite connue, pas un échec silencieux.
- `trident` : la branche latérale, étroite et courte, peut ne trouver AUCUNE
  distance de coupe qui échappe complètement à l'emprise de sa voisine dans
  la plage testée — le garde-fou rejette alors TOUTES les distances
  candidates plutôt que d'accepter une coupe défectueuse, et `split_region`
  reporte honnêtement le chemin en `unresolved_paths` plutôt que de produire
  une région silencieusement fausse. Vérifié : quand une région EST résolue,
  elle est toujours effectivement propre (`junction_count == 0`).

Le coût de ce garde-fou est un `analyze_region` complet (rasterisation +
squelettisation) par candidat testé sur une branche à extrémité réelle — pas
gratuit, mais borné (13 candidats maximum par branche avec les paramètres par
défaut) et seulement pendant la planification, jamais pendant la génération
de points elle-même.

### Phase 5 : Auto-Satin réel par région + Coverage Analyzer comme oracle complet (`libs/satin_planning/region_oracle`)

*État : Présent, testé, aucune régression.*
`libs/satin_planning/include/openstitch/satin_planning/region_oracle.hpp` et
`src/region_oracle.cpp`.

Contrairement à la phase 4 (qui ne vérifie que le STATUT de satinabilité
avant toute génération), cette phase construit réellement les colonnes satin
sur chaque `SatinRegion` (`auto_satin::build_satin_columns`, mode
`Parametric` pour reproduire le comportement de production
d'`autodigitize.cpp` — repli automatique vers `Legacy` déjà géré en interne)
puis mesure la couverture obtenue avec `satin_coverage::analyze_satin_coverage`
— exactement l'oracle demandé par la spec SGSD (§15) : « la preuve finale
reste, est-ce que l'Auto-Satin généré couvre réellement correctement la
région ? ». `satin_coverage` reste totalement indépendant du décomposeur (ne
connaît que des rails/barreaux génériques, jamais un type `auto_satin` ou
`satin_planning`) — même garantie qu'à sa création (§ *Satin Coverage
Analyzer* plus haut).

`evaluate_region_generation(region, genParams, coverageConfig, density)`
convertit la sortie de `build_satin_columns` (`parametric_columns` si non
vide, sinon `columns` — même sélection que `autodigitize.cpp`/le CLI, même
défaut corrigé cette session, § *Balayage du corpus de formes*) en
`satin_coverage::SatinColumnInput`, puis appelle l'analyseur.
`evaluate_decomposition_generation` l'applique à tout un `RegionSplitReport`
et agrège une couverture globale (aire couverte / aire cible sur toutes les
régions).

**Résultats mesurés sur le corpus branché** (mode Parametric, seuils par
défaut de `satin_coverage`, dont `min_core_coverage = 99,5 %`) :

- `t`, `y`, `cross` : chaque région (toutes propres depuis le correctif
  phase 4) atteint **97–99 % de couverture brute/cœur** — cohérent avec la
  limite déjà documentée des résidus de noyau de jonction/bouts (§ *Vérifié*,
  section Satin Coverage Analyzer) : aucune de ces régions ne franchit le
  seuil strict de 99,5 % (`passed = false` partout), mais ce n'est PAS un
  défaut nouveau — c'est la même limite connue de l'Auto-Satin actuel,
  simplement mesurée ici région par région plutôt que sur la forme entière.
- `h` : les deux montants (jamais coupés qu'à une seule extrémité chacun,
  propres dès la phase 4) couvrent aussi **~98 %** — mais **le pont (encore
  branché après découpe, `not_ready` en phase 4) chute à ~70 %** de
  couverture. L'oracle phase 5 confirme AVEC DES CHIFFRES ce que la phase 4
  ne pouvait que signaler qualitativement (statut `RequiresDecomposition`) :
  un écart net et mesurable, pas juste un statut binaire.
- `trident` : la branche isolée avec succès couvre ~98 % ; la branche non
  résolue (§ phase 4, aucune coupe n'échappe à sa voisine) reste
  honnêtement absente du rapport de couverture plutôt que représentée par un
  chiffre inventé.

Ce résultat borne précisément le périmètre encore à couvrir par les phases
suivantes : la phase 6 (recherche à faisceau) pourra comparer plusieurs
découpages candidats en utilisant CET oracle comme fonction de score ; la
phase 7 (fusion) pourra détecter qu'une paire de régions adjacentes aux
couvertures individuellement bonnes redeviendrait encore meilleure fusionnée
(ou, pour le pont du H, qu'une meilleure position de coupe/fusion est
nécessaire avant que sa couverture ne devienne acceptable) ; aucune des deux
n'est encore implémentée à ce stade.

**Testé** (`tests/unit/satin_planning/test_region_oracle.cpp`, 4 cas) :

- `rectangle` : couverture > 90 % sans aucune décomposition.
- Boucle complète sur `t`, `y`, `cross`, `h`, `trident` : chaque région
  effectivement construite produit un rapport de couverture exploitable
  (aire cible non nulle), que le seuil strict soit franchi ou non.
- `h` dédié : vérifie numériquement que le pont couvre nettement moins bien
  (< 85 %) que le meilleur montant (> 95 %) — non-régression directe sur la
  découverte ci-dessus.
- Rendu `format_generation_report` non vide et contenant les marqueurs
  attendus.

### Phase 6 : recherche à faisceau guidée par l'oracle (`libs/satin_planning/beam_search`)

*État : Présent, testé, aucune régression — portée réduite à une décision de
coupe à la fois (voir plus bas).*
`libs/satin_planning/include/openstitch/satin_planning/beam_search.hpp` et
`src/beam_search.cpp`.

Jusqu'ici, `split_region` retenait toujours le premier candidat
géométriquement/topologiquement valide (le plus proche de la jonction,
§ *Phase 3/4*) — un choix arbitraire parmi plusieurs qui passaient déjà les
garde-fous. La phase 6 remplace ce choix par une vraie décision de qualité,
en utilisant l'oracle de la phase 5 pour ARBITRER entre plusieurs candidats
plutôt que d'en accepter un au hasard.

**Portée volontairement réduite** par rapport à la spec (§16) : une recherche
à faisceau complète explorerait des combinaisons de coupes sur PLUSIEURS
jonctions simultanément (la spec avertit elle-même du risque d'explosion
combinatoire). Cette phase se limite à une recherche locale, une décision de
coupe à la fois — un vrai `beamWidth` (candidats évalués, meilleur retenu),
mais appliqué indépendamment à chaque événement de détachement plutôt qu'à
l'arbre complet des décompositions possibles. Une recherche multi-jonctions
arborescente reste un travail futur, explicitement hors périmètre ici.

**Mécanisme** : `region_split.hpp` gagne un point d'injection,
`CutCandidateSelector` (`std::function<optional<size_t>(PathSet, vector<CutCandidate>)>`),
stocké dans `CutCandidateParams::selector` — vide par défaut (comportement
historique inchangé, aucune régression pour les appelants existants). Ce
point d'injection permet à `beam_search` de brancher une stratégie de
sélection SANS que `region_split.hpp`/`.cpp` (qui reste un découpeur de
polygone pur) n'acquière de dépendance vers `region_oracle.hpp` — c'est
`beam_search` qui dépend des deux, jamais l'inverse, la même discipline de
couches que partout ailleurs dans `libs/satin_planning`.

`OracleGuidedSelector` (`operator()` compatible `CutCandidateSelector`, à
injecter via `std::ref`) matérialise, pour chaque événement de détachement,
jusqu'à `beam_width` candidats déjà valides (les plus proches de la jonction
d'abord — le pré-filtrage géométrique/satinabilité de la phase 3/4
s'applique toujours en amont, ce sélecteur ne peut jamais choisir un
candidat que ces garde-fous ont déjà rejeté), construit réellement des
colonnes satin sur chacun et mesure sa couverture (oracle phase 5 complet),
puis retient celui dont la couverture brute est la meilleure. Chaque décision
est journalisée (`decisions()`) pour le debug.

**Résultat mesuré sur le pont du « H »** (le cas le plus dégradé identifié en
phase 5, ~70 % de couverture) : avec `beam_width = 3` et mode `Parametric`,
sa couverture passe de **69,9 % à 92,3 %** — un gain net de plus de
20 points, obtenu SANS modifier le générateur de rails ni la logique de
découpage elle-même, seulement en arbitrant, parmi des positions de coupe
déjà valides, laquelle produit réellement le meilleur résultat une fois
générée. Les deux montants (déjà propres à ~98 %) restent globalement stables
(97,6 % et 98,4 %, les positions de coupe ayant légèrement bougé en
conséquence).

**Coût** : jusqu'à `beam_width` générations + mesures complètes PAR
événement de détachement, en plus des analyses de satinabilité déjà faites
par `generate_cut_candidates` — réservé à un usage hors ligne (planification,
CLI de debug, tests), jamais au chemin interactif.

**Testé** (`tests/unit/satin_planning/test_beam_search.cpp`, 4 cas) :

- `t` : résout toujours la branche, journalise une décision (au plus
  `beam_width` candidats évalués).
- Boucle sur `t`, `y`, `cross`, `h`, `trident` : le nombre de régions
  résolues avec le sélecteur guidé par l'oracle n'est jamais inférieur au
  comportement par défaut (le sélecteur ne peut que choisir MIEUX parmi les
  candidats déjà valides, jamais en révéler de nouveaux).
- `h` : deux décisions journalisées (les deux bouts du pont), au plus
  `beam_width` candidats chacune.
- `h`, non-régression numérique dédiée : confirme le gain de couverture du
  pont (< 80 % avant, > 85 % après, écart > 10 points) — verrouille la
  découverte ci-dessus.

### Phase 7 : passe de fusion post-découpage (`libs/satin_planning/merge_pass`)

*État : Présent, testé, aucune régression — une seule passe, pas de fusion en
cascade (voir plus bas).*
`libs/satin_planning/include/openstitch/satin_planning/merge_pass.hpp` et
`src/merge_pass.cpp`, plus une extension de `region_split.hpp`/`.cpp`
(`RegionSplitReport::merge_candidates`, `MergeCandidate`).

**Repérer les candidats de fusion sans reconstruction géométrique
approximative** — c'est le point délicat de cette phase. Une paire de
régions adjacentes pourrait sembler se recombiner par une simple union
Clipper2, mais la bande retirée par chaque coupe (`cut_width`, 20 µm par
défaut) laisserait un interstice entre les deux morceaux : une union
classique ne les refusionnerait PAS en un seul contour extérieur. La
solution retenue évite le problème plutôt que de le contourner&nbsp;:
`split_region` (phase 3) connaît déjà, à l'instant précis de chaque coupe,
la géométrie EXACTE du morceau juste avant qu'elle ne soit appliquée —
c'est littéralement l'union des deux résultats, sans aucun interstice. Il
suffisait de la conserver. `split_region` trace maintenant, pour chaque
coupe réussie, un petit arbre de filiation (quel morceau vient d'où) et
expose `merge_candidates`&nbsp;: une entrée par coupe dont les DEUX
résultats sont restés des feuilles jusqu'à la fin du découpage (jamais
redécoupés par un événement ultérieur — cf. `cross`, où une seule des deux
coupes qualifie, l'autre ayant son « reste » redécoupé ensuite). Chaque
candidat porte directement la géométrie pré-coupe capturée en mémoire, prête
à être réanalysée sans recalcul.

**Décision de fusion** (`evaluate_merge_pass`) : pour chaque candidat,
construit réellement (oracle phase 5) les deux régions séparées ET la région
fusionnée, compare la couverture obtenue (moyenne pondérée par aire des deux
séparées, contre la couverture unique de la version fusionnée), et
recommande la fusion si l'écart ne dépasse pas une tolérance configurable
(`coverage_tolerance`, 2 points par défaut) — un seul segment vaut une
petite perte de couverture, l'inverse (fusionner malgré une dégradation
importante) non.

**Portée volontairement réduite**, même discipline que la phase 6&nbsp;:
chaque candidat est évalué INDÉPENDAMMENT, sans fusion en cascade (une
région fusionnée n'est jamais elle-même retestée contre un troisième
voisin). La spec (§16/§18) avertit explicitement du risque d'explosion
combinatoire d'une passe itérative « jusqu'à stabilité » sur l'ensemble du
graphe de décomposition — reportée à un travail futur.

**Résultat vérifié sur `t`** : la fusion est correctement **rejetée**
(couverture séparée 97,68&nbsp;%, couverture fusionnée 90,32&nbsp;% — la
version fusionnée réexpose exactement la complexité de jonction que la phase
3 avait résolue en coupant, un écart de 7,4 points bien au-delà de la
tolérance). C'est la preuve que la passe ne réduit pas aveuglément le nombre
de segments&nbsp;: elle mesure réellement, via le même oracle qu'ailleurs
dans le plan SGSD, et ne fusionne QUE quand la qualité le justifie.

**Testé** (`tests/unit/satin_planning/test_merge_pass.cpp`, 4 cas) :

- `t` : exactement un candidat de fusion exposé, dont la géométrie fusionnée
  a une aire égale à l'aire d'origine (preuve indirecte qu'aucun interstice
  de coupe ne subsiste — la reconstruction est exacte, pas une union
  approximative).
- `cross` : sur les deux coupes, une seule expose ses deux enfants comme
  feuilles finales (l'autre a son « reste » redécoupé par l'événement
  suivant) — exactement un candidat de fusion, comme attendu de l'arbre de
  décision décrit ci-dessus.
- `t` : décision de fusion exploitable dans les deux sens (le test ne
  préjuge pas du sens de la recommandation, seulement de la cohérence des
  mesures).
- Rendu `format_merge_pass_report` non vide et contenant les marqueurs
  attendus.

### Phase 8 : recouvrements entre régions adjacentes (`libs/satin_planning/overlap`)

*État : Présent, testé, aucune régression — limité aux paires directement
adjacentes (même restriction qu'à la phase 7, voir plus bas).*
`libs/satin_planning/include/openstitch/satin_planning/overlap.hpp` et
`src/overlap.cpp`.

**Le problème** (§19 spec SGSD) : deux régions séparées exactement sur une
ligne de coupe laisseraient, une fois cousues indépendamment, un interstice
physique visible — la bande retirée par `cut_width` (20 µm par défaut).
Il faut une petite extension de chaque région vers l'autre, mais SANS
modifier les régions structurelles utilisées pour toute mesure de couverture
(phases 4 à 7) : une géométrie dédiée à la broderie, distincte.

**Méthode, entièrement construite sur des primitives déjà existantes** —
aucune nouvelle opération géométrique bas niveau n'a été nécessaire.
Dilater une région de `overlap_distance` est un simple appel à
`geometry::inset_path_set` avec un delta NÉGATIF (l'outil existant
d'érosion/dilatation, déjà utilisé pour les sous-couches). Reste à garantir
que l'extension « reste dans la shape originale » (§19) sans reconstruire une
ligne de coupe déplacée : la région dilatée est recadrée
(`geometry::intersect_polygons`) directement dans `MergeCandidate::merged_region`
— la géométrie EXACTE d'avant coupe, déjà capturée sans interstice à la
phase 7. Deux appels à des primitives existantes suffisent donc à produire
une extension géométriquement correcte et bornée.

**Résultat vérifié sur `t`** (`overlap_distance` = 300 µm par défaut) :
chaque région élargie est strictement plus grande que l'originale, jamais
au-delà de la géométrie fusionnée de référence, et surtout — la preuve
centrale de cette phase — **les deux régions élargies se chevauchent
réellement désormais** (aire d'intersection mesurée : 2,95 mm²), là où les
régions structurelles d'origine ne se touchaient pas (séparées par
`cut_width`). L'interstice est fermé.

**Limite connue**, même restriction qu'à la phase 7 et pour la même raison :
ne couvre que les paires DIRECTEMENT adjacentes dont les deux côtés sont
restés des feuilles jusqu'à la fin du découpage
(`RegionSplitReport::merge_candidates`, réutilisé tel quel — c'est déjà
exactement l'ensemble des paires dont la géométrie exacte d'avant coupe est
connue). Une couture plus profonde dans l'arbre de décision (un côté
redécoupé ensuite, ex. le pont d'un « H » vis-à-vis d'un montant) n'a pas
encore de mécanisme de recouvrement — déterminer l'adjacence finale au-delà
d'une seule coupe reste un travail futur. La densité de points dans la zone
de recouvrement (§19 : « ne pas créer une surdensité énorme ») est hors
périmètre de cette phase, purement géométrique — à traiter quand
`stitch_generation` consommera cette géométrie dédiée.

**Testé** (`tests/unit/satin_planning/test_overlap.cpp`, 3 cas) :

- `t` : aire élargie strictement supérieure à l'originale pour les deux
  régions, jamais au-delà de la géométrie fusionnée, ET intersection non
  vide entre les deux régions élargies (l'invariant central : l'interstice
  est bien fermé).
- `cross` : un recouvrement généré par paire directement adjacente, en
  correspondance exacte avec `merge_candidates`.
- Rendu `format_overlap_report` non vide et contenant les marqueurs attendus.

## Implémentation associée

- `libs/document/.../embroidery_object.hpp` — `SatinParams`, `SatinRung`.
- `libs/stitch_generation/src/satin.cpp` — `fill_satin`, `fill_satin_columns`
  (par barreaux), `rails_from_contour`, `ladder_correspondence` +
  `resample_by_medial_spacing` (correspondance locale rail A/rail B, audit
  rails 2026-08-01).
- `libs/stitch_generation/src/satin_guides.cpp` — projection sur rail et
  validation monotone partagées par l'éditeur de guides.
- `libs/stitch_generation/src/generate.cpp` — `generate_satin` (route vers
  `fill_satin_columns` si barreaux présents, oriente par entrée/sortie, émet les
  locks).
- `libs/stitch_generation/src/lock.cpp` — `lock_stitches`, `LockType` (Lot 5).
- `libs/stitch_generation/src/routing.cpp` — `route_columns`, `RoutePlan`
  (ordre/orientation/liaisons, Lot 6) ; `generate_satin_group` dans
  `generate.cpp` (émission des groupes routés).
- `libs/auto_satin/.../satin_column.hpp` + `src/satin_column.cpp` —
  `build_satin_columns`, `SatinColumnGeometry`, `SatinRung`, `JunctionCore`
  (moteur géométrique) ; `extend_tip` (bouts ouverts) et
  `trim_unstable_junction_tail`/`representative_station_width` (amputation de
  la queue instable d'un bout de jonction jusqu'à sa dernière section
  réellement stable) ; `cross_section`/`CrossSectionFailure`,
  `interpolate_station`/`interpolated_station_valid` (assemblage des stations
  tolérant aux trous, audit génération partielle 2026-08-03) ;
  `collect_junction_branches`/`StableBranchEnd`/`make_stable_branch_end`/
  `reflex_vertices`/`build_separator`/`build_sector`/`resolve_junction`
  (partition StableBranchEnd/JunctionSeparator/secteurs par jonction, sans
  mutation de rail, noyau calculé comme `local_region − union(secteurs)` —
  remplace l'optimisation combinatoire de bridges, 2026-08-04) ;
  `station_point`/`outward_tangent_at` (variantes à `offset` de
  `terminal_point`/`outward_tangent`, retractation itérative d'une
  `StableBranchEnd` qui croise une branche voisine) ; `polygon_self_intersects` ;
  `ContourPolyline`/`project_to_contour`/`extract_contour_arc` (utilitaires de
  contour, diagnostic uniquement, aussi utilisés pour la recherche de
  séparateur par contiguïté de contour). Tout ce paragraphe est le moteur
  `Legacy` (`SatinGeometryMode::Legacy`, mode par défaut), inchangé.
- `libs/auto_satin/.../satin_column.hpp` + `src/satin_column.cpp` — mode
  `Parametric` (refonte 2026-08-04) : `ParametricSatinObject`,
  `SatinControlPair`, `SatinAngleGuide`, `SatinJunctionPlan`,
  `SatinGeometryMode` ; `compute_column_stations` (couche d'analyse dense
  PARTAGÉE avec `Legacy`, extraite de l'ancien `build_column`) ;
  `select_structural_indices`/`width_extrema_indices`/
  `adjacent_width_jump_indices`/`curvature_maxima_indices`/
  `douglas_peucker_indices` (sélection des paires structurantes) ;
  `fit_cubic_segment`/`cubic_eval`/`cubic_point_distance` (moindres carrés à
  tangentes fixées, longueur de poignée = projection de la corde sur la
  tangente) ; `fit_both_rails`/`fit_both_rails_recursive`/`JointRailSegment`/
  `RailFit` (ajustement conjoint des deux rails, ancrages partagés,
  subdivision adaptative) ; `rail_bezier_path`/`build_control_pairs_and_guides` ;
  `build_parametric_object` (finalisation paramétrique complète d'une
  branche) ; `extend_into_confluence`/`junction_overlap_target` (recouvrement
  local de jonction, remplace `resolve_junction` pour ce mode) ;
  `plan_junction_stitch_order` (§ étape 10) ;
  `validate_parametric_object`/`distance_to_polys` (§ étape 12).
- `libs/auto_satin/src/skeleton_graph.cpp` — `build_skeleton_graph` :
  consolidation des amas de pixels de jonction 8-connexes en un seul
  `SkeletonNode` (`DisjointSet`), suppression des micro-arêtes internes à
  l'amas (audit jonctions branchées/concaves, 2026-08-03).
- `libs/auto_satin/src/debug_export.cpp` — `columns_to_svg` (mode `Legacy`) ;
  `parametric_to_svg` (mode `Parametric`, 2026-08-04 : rails aplatis,
  poignées Bézier, paires structurantes, recouvrement, ordre de couture,
  statistiques par objet en commentaire).
- `libs/auto_satin/.../shapes.hpp` + `src/shapes.cpp` — corpus de formes de
  test (`make_shape`), dont `notch`/`pinch` (encoche concave, audit génération
  partielle 2026-08-03) et `trident` (jonction concave asymétrique à 3
  branches inégales, audit jonctions branchées/concaves, 2026-08-03).
- `apps/cli/main.cpp` — `run_auto_satin_debug` : option `--satin-geometry=
  legacy|parametric` (2026-08-04, défaut `legacy`).
- Tests : `tests/unit/stitch/test_satin.cpp` (dont « rail Bezier eparse ->
  points suivent la courbe, pas la corde », 2026-08-04 — vérifie que
  `fill_satin_columns`/`to_points` aplatit réellement un rail à tangentes),
  `tests/unit/stitch/test_satin_pairing_metrics.cpp` (fixtures et métriques de
  l'appariement, audit rails 2026-08-01),
  `tests/unit/auto_satin/test_columns.cpp` (§ « parametrique : ... », six
  nouveaux cas 2026-08-04 : simplification réelle, embouts arrondis dans la
  région, trident à 3 objets sans croisement, recouvrement borné,
  correspondance monotone, lignes d'angle non croisées),
  `tests/unit/auto_satin/test_pipeline.cpp`.
- `libs/satin_coverage/include/openstitch/satin_coverage/coverage.hpp` +
  `src/coverage.cpp` — `analyze_satin_coverage`, `SatinColumnInput`,
  `SatinCoverageReport`, `MissingRegion`, `SatinCoverageConfig` (Satin
  Coverage Analyzer, 2026-08-13, § ci-dessus).
- `libs/stitch_generation/include/openstitch/stitch_generation/satin.hpp` +
  `src/satin.cpp` — `SatinStation`, `satin_stations` (correspondance
  structurelle rail A/rail B exposée publiquement, extraite sans changement de
  comportement de `fill_satin_columns`, 2026-08-13).
- `libs/geometry/include/openstitch/geometry/boolean.hpp` + `src/boolean.cpp`
  — `path_set_area_um2`, `intersect_polygons`, `difference_polygons` (booléens
  NonZero généralisés `vector<PathSet> × vector<PathSet>`, trous compris des
  deux côtés, 2026-08-13).
- `libs/geometry/src/offset.cpp` — `inset_path_set`, réoriente désormais
  chaque contour (`oriented_as`) avant Clipper2 (correctif balayage
  auto-satin, 2026-08-13).
- `libs/auto_satin/src/shapes.cpp` — trou de `ring` réorienté à la
  construction (2026-08-13).
- Tests : `tests/unit/satin_coverage/test_coverage.cpp` ;
  `tests/unit/auto_satin/test_coverage_regression.cpp` (non-régression sur le
  corpus de formes historiques, 2026-08-13) ;
  `tests/unit/geometry/test_offset.cpp` (érosion d'un trou de même
  orientation que son extérieur, 2026-08-13) ;
  `tests/integration/test_pipeline.cpp` (« DIAGNOSTIC TEMPORAIRE couverture
  satin (tentabrode) », combinaison satin + repli tatami par `source_region`
  sur une image réelle, 2026-08-13, § ci-dessus).
- `libs/satin_planning/include/openstitch/satin_planning/branch_pairing.hpp` +
  `src/branch_pairing.cpp` — `continuation_cost`, `pair_branches_at_junction`,
  `decompose_into_paths`, `format_decomposition_report`, `SatinPath`,
  `JunctionPairingReport` (SGSD phase 1, appariement de branches au-dessus du
  `SkeletonGraph` existant, 2026-08-13, § ci-dessus) ; `find_edge`/`find_node`
  (recherche par id, exposées publiquement pour réutilisation par
  `region_split`).
- `libs/satin_planning/include/openstitch/satin_planning/region_split.hpp` +
  `src/region_split.cpp` — `generate_cut_candidates`, `split_region`,
  `format_region_split_report`, `SatinRegion`, `CutCandidate`, `CutAttempt`
  (SGSD phase 3, candidats de coupe + découpage réel du polygone via
  `geometry::cut_path_set`, 2026-08-13, § ci-dessus) ; garde-fou de
  satinabilité intégré à `generate_cut_candidates` + plage de recherche
  élargie (`CutCandidateParams::search_max_um` 1500→4000µm, correctif phase 4,
  2026-08-13, § ci-dessus).
- `libs/satin_planning/include/openstitch/satin_planning/region_satinability.hpp`
  et `src/region_satinability.cpp` — `check_region_satinability`,
  `check_all_regions`, `format_satinability_report`, `RegionSatinabilityVerdict`
  (SGSD phase 4, réanalyse de satinabilité d'une `SatinRegion` déjà découpée
  via `auto_satin::analyze_region`, 2026-08-13, § ci-dessus).
- `libs/satin_planning/include/openstitch/satin_planning/region_oracle.hpp`
  et `src/region_oracle.cpp` — `evaluate_region_generation`,
  `evaluate_decomposition_generation`, `format_generation_report`,
  `RegionGenerationVerdict` (SGSD phase 5, `build_satin_columns` réel +
  `satin_coverage::analyze_satin_coverage` par région, 2026-08-13,
  § ci-dessus).
- `libs/satin_planning/include/openstitch/satin_planning/region_split.hpp`
  — ajoute `CutCandidateSelector` + `CutCandidateParams::selector` (point
  d'injection phase 6, vide par défaut, comportement historique inchangé,
  2026-08-13, § ci-dessus).
- `libs/satin_planning/include/openstitch/satin_planning/beam_search.hpp`
  et `src/beam_search.cpp` — `OracleGuidedSelector`, `BeamSearchParams`,
  `BeamCandidateScore` (SGSD phase 6, recherche à faisceau locale guidée par
  l'oracle phase 5, gain mesuré de 69,9→92,3 % sur le pont du « H »,
  2026-08-13, § ci-dessus).
- `libs/satin_planning/include/openstitch/satin_planning/region_split.hpp`
  — ajoute `MergeCandidate` + `RegionSplitReport::merge_candidates`, tracés
  par un petit arbre de filiation interne à `split_region` (phase 7,
  2026-08-13, § ci-dessus).
- `libs/satin_planning/include/openstitch/satin_planning/merge_pass.hpp`
  et `src/merge_pass.cpp` — `evaluate_merge_pass`, `format_merge_pass_report`,
  `MergeDecision`, `MergePassParams` (SGSD phase 7, décision de fusion
  guidée par l'oracle phase 5, 2026-08-13, § ci-dessus).
- `libs/satin_planning/include/openstitch/satin_planning/overlap.hpp` et
  `src/overlap.cpp` — `generate_overlaps`, `format_overlap_report`,
  `RegionOverlap`, `OverlapParams` (SGSD phase 8, dilatation
  `geometry::inset_path_set` recadrée dans `MergeCandidate::merged_region`,
  aucune nouvelle primitive géométrique bas niveau, 2026-08-13, § ci-dessus).
- Tests : `tests/unit/satin_planning/test_branch_pairing.cpp`,
  `tests/unit/satin_planning/test_region_split.cpp`,
  `tests/unit/satin_planning/test_region_satinability.cpp`,
  `tests/unit/satin_planning/test_region_oracle.cpp`,
  `tests/unit/satin_planning/test_beam_search.cpp`,
  `tests/unit/satin_planning/test_merge_pass.cpp`,
  `tests/unit/satin_planning/test_overlap.cpp`.
