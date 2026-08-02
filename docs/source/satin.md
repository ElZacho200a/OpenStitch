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

**Limite connue, non corrigée ici** : la forme `cross` (croix à 4 branches)
produit actuellement une topologie de squelette différente de l'attendu (un
nœud de degré 2 au lieu d'un nœud de jonction unique de degré 4, stable sur
toutes les résolutions de rasterisation testées) — un défaut distinct, situé
dans l'extraction du graphe de squelette (`skeleton_graph.cpp`), pas dans
l'ancrage des rails documenté ici. Non instruit dans ce lot.

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

Vérifié : center/edge/zigzag = 4 passes distinctes ordonnées, pull élargit **un
seul côté** (asymétrique), push étend le bout, déterminisme, aller-retour `.osp`
(SVG `tests/golden/auto-satin/lot4-*.svg` : sous-couches en vert).

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
- **Liaisons** : une transition courte (≤ `underpath_max`, défaut 8 mm) est
  cousue en **trajet caché** (passe `Travel`, running stitch — pas de coupe) ;
  au-delà, elle reste un **saut**. Minimise les coupes et les déplacements à
  découvert.

Une jonction déclarée mais séparée de plus de `underpath_max` est traitée comme
incohérente et n'influence ni l'ordre ni le type de liaison. Cette garde évite
qu'un `.osp` altéré force un trajet caché arbitraire. Limite actuelle : le trajet
entre deux sections reste un segment échantillonné ; il ne suit pas encore le
centre d'une branche déjà cousue lors d'un retour vers une jonction.

Le groupe routé est **contigu** et limité aux colonnes auto (porteuses de
barreaux) de couleur et source identiques : l'ordre inter-groupes et le reste du
document sont préservés. Un satin manuel isolé n'est pas réordonné.

Vérifié : liste vide → plan vide ; une colonne → liaison de départ, aucun saut ;
réordonnancement minimisant le déplacement ; orientation par l'extrémité proche ;
liaison longue → saut, liaison courte → trajet caché ; à la génération, un groupe
adjacent n'émet qu'un saut initial (le reste cousu), un groupe éloigné conserve
ses sauts (SVG `tests/golden/auto-satin/lot6-route-*.svg` : trajets cachés en
bleu, sauts en rouge pointillé).

Limite : le trajet caché est un segment **direct** (échantillonné en points
cousus) ; il n'est garanti *sous* la broderie que pour des colonnes adjacentes
(le seuil `underpath_max` bascule les liaisons douteuses en sauts). Un routage
suivant réellement la matière viendra avec le tatami avancé (Lot 7).

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
  `build_satin_columns`, `SatinColumnGeometry`, `SatinRung` (moteur géométrique) ;
  `extend_tip` (bouts ouverts) et `reflex_vertices`/`trim_and_anchor_junction_end`
  (bouts de jonction, mission « auto-satin béton »).
- `libs/auto_satin/src/debug_export.cpp` — `columns_to_svg`.
- Tests : `tests/unit/stitch/test_satin.cpp`,
  `tests/unit/stitch/test_satin_pairing_metrics.cpp` (fixtures et métriques de
  l'appariement, audit rails 2026-08-01),
  `tests/unit/auto_satin/test_columns.cpp`.
