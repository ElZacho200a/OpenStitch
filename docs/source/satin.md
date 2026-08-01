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

- l'auto-numérisation **n'utilise plus** le satin naïf par défaut ; toute zone
  remplissable devient un **tatami** (découpé au scanline sur la région, donc
  sans débordement). L'option `AutoOptions::use_naive_satin` (défaut `false`)
  permet de le réactiver pour essais/tests ;
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
colonne. Les rails **se terminent sur le contour** → pas de débordement structurel.

Décision : `Suitable` → une colonne (axe principal) ; `RequiresDecomposition`
(Y/T) → une colonne par branche menant à une extrémité ; `Ambiguous` (quasi
circulaire), `Unsuitable` (trou, trop large/étroite) → **refus explicite**.
Déterministe. Vérifié visuellement via `openstitch-cli auto-satin-debug --shape
<forme> --output-svg` (SVG dans `tests/golden/auto-satin/`).

Intégration : **Broderie ▸ Convertir automatiquement en satin…** affiche la
satinabilité (statut, confiance, largeurs, branches, colonnes) puis crée les
objets satin (annulable). Les **barreaux sont stockés** dans `SatinParams.rungs`
et sérialisés (`.osp` schéma v2, rétrocompatible).

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

- **Ordre** : glouton plus proche voisin depuis la position courante de
  l'aiguille (on entre par l'extrémité la plus proche), puis amélioration
  **2-opt**. Déterministe.
- **Orientation** : pour un ordre donné, le sens de chaque colonne est résolu
  **exactement** par programmation dynamique sur ses deux extrémités
  (l'orientation choisie est imposée à `generate_satin` via entrée/sortie).
- **Liaisons** : une transition courte (≤ `underpath_max`, défaut 8 mm) est
  cousue en **trajet caché** (passe `Travel`, running stitch — pas de coupe) ;
  au-delà, elle reste un **saut**. Minimise les coupes et les déplacements à
  découvert.

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
- `libs/stitch_generation/src/generate.cpp` — `generate_satin` (route vers
  `fill_satin_columns` si barreaux présents, oriente par entrée/sortie, émet les
  locks).
- `libs/stitch_generation/src/lock.cpp` — `lock_stitches`, `LockType` (Lot 5).
- `libs/stitch_generation/src/routing.cpp` — `route_columns`, `RoutePlan`
  (ordre/orientation/liaisons, Lot 6) ; `generate_satin_group` dans
  `generate.cpp` (émission des groupes routés).
- `libs/auto_satin/.../satin_column.hpp` + `src/satin_column.cpp` —
  `build_satin_columns`, `SatinColumnGeometry`, `SatinRung` (moteur géométrique).
- `libs/auto_satin/src/debug_export.cpp` — `columns_to_svg`.
- Tests : `tests/unit/stitch/test_satin.cpp`,
  `tests/unit/stitch/test_satin_pairing_metrics.cpp` (fixtures et métriques de
  l'appariement, audit rails 2026-08-01),
  `tests/unit/auto_satin/test_columns.cpp`.
